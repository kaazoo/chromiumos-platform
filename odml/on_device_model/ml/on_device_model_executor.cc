// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/on_device_model/ml/on_device_model_executor.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/compiler_specific.h>
#include <base/containers/unique_ptr_adapters.h>
#include <base/logging.h>
#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/numerics/safe_conversions.h>
#include <base/task/thread_pool.h>
#include <base/timer/elapsed_timer.h>
#include <metrics/metrics_library.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/mojom/on_device_model_service.mojom.h"
#include "odml/on_device_model/ml/chrome_ml.h"
#include "odml/on_device_model/ml/session_accessor.h"

using on_device_model::mojom::LoadModelResult;

namespace ml {
namespace {

constexpr uint32_t kReserveTokensForSafety = 2;

template <typename T>
struct ConstData {
  T data;
  constexpr T Get() const { return data; }
};

constexpr ConstData<int> kMaxTopK{128};

constexpr ConstData<bool> kPreferTextureWeights{true};

constexpr ConstData<bool> kEnableHostMappedPointer{true};

constexpr ConstData<bool> kUseLowPower{false};

constexpr ConstData<bool> kAllowFp16{true};

void ReportHistogramCounts10000(raw_ref<MetricsLibraryInterface> metrics,
                                std::string_view name,
                                int sample) {
  metrics->SendToUMA(std::string(name), sample, 1, 10000, 50);
}

// Helper to bind object methods as weak task-posting callback functions.
template <typename R, typename C, typename... Args>
std::function<R(Args...)> CreateWeakCallbackFn(R (C::*method)(Args...),
                                               C* that) {
  return [weak_ptr = that->AsWeakPtr(), method,
          task_runner =
              base::SequencedTaskRunner::GetCurrentDefault()](Args&&... args) {
    task_runner->PostTask(
        FROM_HERE,
        base::BindOnce(method, weak_ptr, std::forward<Args>(args)...));
  };
}

// Helper to convert a OnceCallback to std::function.
template <typename R, typename... Args>
std::function<R(Args...)> ConvertCallbackToFn(
    base::OnceCallback<R(Args...)> callback) {
  auto shared_callback =
      std::make_shared<base::OnceCallback<R(Args...)>>(std::move(callback));
  return [shared_callback = shared_callback,
          task_runner =
              base::SequencedTaskRunner::GetCurrentDefault()](Args&&... args) {
    if (!shared_callback->is_null()) {
      task_runner->PostTask(FROM_HERE,
                            base::BindOnce(std::move(*shared_callback),
                                           std::forward<Args>(args)...));
    }
  };
}

int CalculateTokensPerSecond(int num_tokens, base::TimeDelta duration) {
  if (duration.InMicroseconds() <= 0) {
    return 0;
  }
  return (num_tokens / static_cast<float>(duration.InMicroseconds())) *
         base::Time::kMicrosecondsPerSecond;
}

float GetTemperature(std::optional<float> temperature) {
  return std::max(0.0f, temperature.value_or(0.0f));
}

uint32_t GetTopK(std::optional<uint32_t> top_k) {
  return std::min(static_cast<uint32_t>(kMaxTopK.Get()),
                  std::max(1u, top_k.value_or(1)));
}

std::optional<ModelBackendType> ModelBackendTypeFromMojom(
    on_device_model::mojom::ModelBackendType backend) {
  switch (backend) {
    case on_device_model::mojom::ModelBackendType::kGpu:
      return ModelBackendType::kGpuBackend;
    case on_device_model::mojom::ModelBackendType::kApu:
      return ModelBackendType::kApuBackend;
    default:
      return std::nullopt;
  }
}

}  // namespace

// Handles sending and canceling responses.
class Responder final {
 public:
  explicit Responder(
      raw_ref<MetricsLibraryInterface> metrics,
      mojo::PendingRemote<on_device_model::mojom::StreamingResponder> responder,
      base::OnceClosure on_complete,
      SessionAccessor::Ptr session)
      : metrics_(metrics),
        responder_(std::move(responder)),
        on_complete_(std::move(on_complete)),
        session_(std::move(session)) {
    responder_.set_disconnect_handler(
        base::BindOnce(&Responder::Cancel, base::Unretained(this)));
  }
  ~Responder() { Cancel(); }

  ChromeMLCancelFn* GetCancelFn() { return &cancel_; }

  ChromeMLExecutionOutputFn CreateOutputFn() {
    return [weak_ptr = weak_ptr_factory_.GetWeakPtr(),
            task_runner = base::SequencedTaskRunner::GetCurrentDefault()](
               const ChromeMLExecutionOutput* output) {
      std::optional<std::string> text;
      switch (output->status) {
        case ChromeMLExecutionStatus::kInProgress:
          CHECK(output->text);
          text.emplace(output->text);
          break;
        case ChromeMLExecutionStatus::kComplete:
          DCHECK(!output->text);
          break;
      }

      task_runner->PostTask(
          FROM_HERE,
          base::BindOnce(&Responder::OnOutput, weak_ptr, std::move(text)));
    };
  }

 private:
  void OnOutput(std::optional<std::string> text) {
    if (text) {
      num_tokens_++;
      output_so_far_ += *text;
      if (first_token_time_ == base::TimeTicks()) {
        first_token_time_ = base::TimeTicks::Now();
      }

      auto chunk = on_device_model::mojom::ResponseChunk::New();
      chunk->text = *text;
      responder_->OnResponse(std::move(chunk));
    } else {
      // Empty text means the output is finished. Delete the session immediately
      // to free up any resources.
      session_ = nullptr;
      ReportHistogramCounts10000(metrics_, "OnDeviceModel.TokenCount.Output",
                                 num_tokens_);
      if (num_tokens_ > 1) {
        // Time starts at the first token to avoid counting input processing
        // time, so calculate using num_tokens_ - 1.
        ReportHistogramCounts10000(
            metrics_, "OnDeviceModel.TokensPerSecond.Output",
            CalculateTokensPerSecond(
                num_tokens_ - 1, base::TimeTicks::Now() - first_token_time_));
      }

      auto summary = on_device_model::mojom::ResponseSummary::New();
      responder_->OnComplete(std::move(summary));
      if (!on_complete_.is_null()) {
        std::move(on_complete_).Run();
      }
    }
  }

  void Cancel() {
    session_ = nullptr;
    if (cancel_) {
      cancel_();
    }
    if (!on_complete_.is_null()) {
      std::move(on_complete_).Run();
    }
  }

  const raw_ref<MetricsLibraryInterface> metrics_;
  base::TimeTicks first_token_time_;
  int num_tokens_ = 0;
  std::string output_so_far_;
  mojo::Remote<on_device_model::mojom::StreamingResponder> responder_;
  ChromeMLCancelFn cancel_;
  base::OnceClosure on_complete_;
  SessionAccessor::Ptr session_;
  base::WeakPtrFactory<Responder> weak_ptr_factory_{this};
};

// Handles calling the ContextClient on completion and canceling the context
// request.
class ContextHolder final {
 public:
  explicit ContextHolder(
      raw_ref<MetricsLibraryInterface> metrics,
      mojo::PendingRemote<on_device_model::mojom::ContextClient> client,
      base::OnceCallback<void(ContextHolder*)> on_disconnect,
      base::OnceClosure on_complete)
      : metrics_(metrics),
        client_(std::move(client)),
        on_disconnect_(std::move(on_disconnect)),
        on_complete_(std::move(on_complete)) {
    if (client_) {
      client_.set_disconnect_handler(
          base::BindOnce(&ContextHolder::OnDisconnect, base::Unretained(this)));
    }
  }
  ~ContextHolder() {
    if (cancel_) {
      cancel_();
    }
    if (!on_complete_.is_null()) {
      std::move(on_complete_).Run();
    }
  }

  ChromeMLCancelFn* GetCancelFn() { return &cancel_; }

  ChromeMLContextSavedFn CreateContextSavedFn() {
    return CreateWeakCallbackFn(&ContextHolder::OnComplete, this);
  }

  base::WeakPtr<ContextHolder> AsWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

 private:
  void OnComplete(int tokens_processed) {
    if (tokens_processed > 0) {
      ReportHistogramCounts10000(metrics_, "OnDeviceModel.TokenCount.Context",
                                 tokens_processed);
      ReportHistogramCounts10000(
          metrics_, "OnDeviceModel.TokensPerSecond.Context",
          CalculateTokensPerSecond(tokens_processed, timer_.Elapsed()));
    }
    if (client_) {
      client_->OnComplete(tokens_processed);
    }
    if (!on_complete_.is_null()) {
      std::move(on_complete_).Run();
    }
    OnDisconnect();
  }

  void OnDisconnect() {
    if (on_disconnect_) {
      std::move(on_disconnect_).Run(this);
    }
    // this may be deleted.
  }

  const raw_ref<MetricsLibraryInterface> metrics_;
  base::ElapsedTimer timer_;
  mojo::Remote<on_device_model::mojom::ContextClient> client_;
  base::OnceCallback<void(ContextHolder*)> on_disconnect_;
  ChromeMLCancelFn cancel_;
  base::OnceClosure on_complete_;
  base::WeakPtrFactory<ContextHolder> weak_ptr_factory_{this};
};

SessionImpl::SessionImpl(raw_ref<MetricsLibraryInterface> metrics,
                         const ChromeML& chrome_ml,
                         ChromeMLModel model,
                         SessionAccessor::Ptr session,
                         SessionAccessor::Ptr empty_session,
                         uint32_t max_tokens,
                         std::optional<uint32_t> adaptation_id)
    : metrics_(metrics),
      chrome_ml_(chrome_ml),
      model_(model),
      session_(std::move(session)),
      empty_session_(std::move(empty_session)),
      max_tokens_(max_tokens),
      adaptation_id_(adaptation_id) {}
SessionImpl::~SessionImpl() = default;

DISABLE_CFI_DLSYM
void SessionImpl::AddContext(
    on_device_model::mojom::InputOptionsPtr input,
    mojo::PendingRemote<on_device_model::mojom::ContextClient> client,
    base::OnceClosure on_complete) {
  auto context_holder = std::make_unique<ContextHolder>(
      metrics_, std::move(client),
      base::BindOnce(&SessionImpl::RemoveContext, base::Unretained(this)),
      std::move(on_complete));
  input->max_tokens =
      std::min(input->max_tokens.value_or(max_tokens_), max_tokens_);
  input->top_k = GetTopK(input->top_k);
  input->temperature = GetTemperature(input->temperature);
  ChromeMLContextSavedFn context_saved_fn =
      context_holder->CreateContextSavedFn();
  *context_holder->GetCancelFn() =
      session_->Execute(std::move(input), nullptr, context_saved_fn);
  context_holders_.insert(std::move(context_holder));
}

DISABLE_CFI_DLSYM
void SessionImpl::Execute(
    on_device_model::mojom::InputOptionsPtr input,
    mojo::PendingRemote<on_device_model::mojom::StreamingResponder> response,
    base::OnceClosure on_complete) {
  auto cloned =
      input->ignore_context ? empty_session_->Clone() : session_->Clone();
  auto cloned_raw = cloned.get();  // For Execute after std::move
  responder_ = std::make_unique<Responder>(
      metrics_, std::move(response), std::move(on_complete), std::move(cloned));
  ChromeMLExecutionOutputFn output_fn = responder_->CreateOutputFn();
  input->max_tokens =
      std::min(input->max_tokens.value_or(max_tokens_), max_tokens_);
  input->top_k = GetTopK(input->top_k);
  input->temperature = GetTemperature(input->temperature);
  *responder_->GetCancelFn() =
      cloned_raw->Execute(std::move(input), output_fn, nullptr);
}

DISABLE_CFI_DLSYM
void SessionImpl::SizeInTokens(on_device_model::mojom::InputPtr input,
                               base::OnceCallback<void(uint32_t)> callback) {
  session_->SizeInTokens(std::move(input),
                         ConvertCallbackToFn(std::move(callback)));
}

DISABLE_CFI_DLSYM
void SessionImpl::Score(const std::string& text,
                        base::OnceCallback<void(float)> callback) {
  session_->Score(text, ConvertCallbackToFn(std::move(callback)));
}

std::unique_ptr<SessionImpl> SessionImpl::Clone() {
  return std::make_unique<SessionImpl>(
      metrics_, chrome_ml_.get(), model_, session_->Clone(),
      empty_session_->Clone(), max_tokens_, adaptation_id_);
}

void SessionImpl::RemoveContext(ContextHolder* context) {
  std::erase_if(context_holders_, base::MatchesUniquePtr(context));
}

DISABLE_CFI_DLSYM
void DestroyModel(const ChromeML* chrome_ml, ChromeMLModel model) {
  chrome_ml->api().DestroyModel(model);
}

OnDeviceModelExecutor::OnDeviceModelExecutor(
    raw_ref<MetricsLibraryInterface> metrics,
    base::PassKey<OnDeviceModelExecutor>,
    const ChromeML& chrome_ml)
    : metrics_(metrics),
      chrome_ml_(chrome_ml),
      model_task_runner_(
          base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})) {}

OnDeviceModelExecutor::~OnDeviceModelExecutor() {
  if (model_ != 0) {
    model_task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&DestroyModel, &chrome_ml_.get(), model_));
  }
}

// static
base::expected<std::unique_ptr<OnDeviceModelExecutor>, LoadModelResult>
OnDeviceModelExecutor::CreateWithResult(
    raw_ref<MetricsLibraryInterface> metrics,
    const ChromeML& chrome_ml,
    on_device_model::mojom::LoadModelParamsPtr params,
    base::OnceClosure on_complete) {
  auto executor = std::make_unique<OnDeviceModelExecutor>(
      metrics, base::PassKey<OnDeviceModelExecutor>(), chrome_ml);
  auto load_model_result =
      executor->Init(std::move(params), std::move(on_complete));
  if (load_model_result == LoadModelResult::kSuccess) {
    return base::ok<std::unique_ptr<OnDeviceModelExecutor>>(
        std::move(executor));
  }
  return base::unexpected(load_model_result);
}

std::unique_ptr<SessionImpl> OnDeviceModelExecutor::CreateSession(
    std::optional<uint32_t> adaptation_id) {
  auto it = base_sessions_.find(adaptation_id);
  CHECK(it != base_sessions_.end());
  return std::make_unique<SessionImpl>(
      metrics_, *chrome_ml_, model_, it->second->Clone(), it->second->Clone(),
      max_tokens_ - kReserveTokensForSafety, adaptation_id);
}

DISABLE_CFI_DLSYM
base::expected<uint32_t, LoadModelResult> OnDeviceModelExecutor::LoadAdaptation(
    on_device_model::mojom::LoadAdaptationParamsPtr params,
    base::OnceClosure on_complete) {
  on_device_model::AdaptationAssets assets = std::move(params->assets);
  static uint32_t next_id = 0;
  base_sessions_.insert(
      {next_id, SessionAccessor::Create(chrome_ml_.get(), model_task_runner_,
                                        model_, std::move(assets))});
  model_task_runner_->PostTask(FROM_HERE, std::move(on_complete));
  return base::ok(next_id++);
}

DISABLE_CFI_DLSYM
LoadModelResult OnDeviceModelExecutor::Init(
    on_device_model::mojom::LoadModelParamsPtr params,
    base::OnceClosure on_complete) {
  on_device_model::ModelAssets assets = std::move(params->assets);

  max_tokens_ = std::max(params->max_tokens, kReserveTokensForSafety);

  std::optional<ModelBackendType> backend_type =
      ModelBackendTypeFromMojom(params->backend_type);
  if (!backend_type.has_value()) {
    LOG(ERROR) << "Failed to parse model backend type";
    return LoadModelResult::kFailedToLoadLibrary;
  }

  ChromeMLModelData data;
  std::string weights_path_str = assets.weights_path.AsUTF8Unsafe();
  std::string sp_model_path_str = assets.sp_model_path.AsUTF8Unsafe();
  if (*backend_type == ModelBackendType::kGpuBackend) {
    data.weights_file = assets.weights.TakePlatformFile();
  } else {
    data.model_path = weights_path_str.data();
    data.sentencepiece_model_path = sp_model_path_str.data();
  }
  ChromeMLModelDescriptor descriptor = {
      .backend_type = *backend_type,
      .model_data = &data,
      .max_tokens = max_tokens_,
      .temperature = 0.0f,
      .top_k = kMaxTopK.Get(),
      .adaptation_ranks = params->adaptation_ranks.data(),
      .adaptation_ranks_size = params->adaptation_ranks.size(),
      .prefer_texture_weights = kPreferTextureWeights.Get(),
      .enable_host_mapped_pointer = kEnableHostMappedPointer.Get(),
      .use_low_power = kUseLowPower.Get(),
      .allow_fp16 = kAllowFp16.Get(),
  };
  model_ = chrome_ml_->api().SessionCreateModel(
      &descriptor, reinterpret_cast<uintptr_t>(this),
      OnDeviceModelExecutor::Schedule);
  if (model_) {
    base_sessions_.insert(
        {std::nullopt, SessionAccessor::Create(chrome_ml_.get(),
                                               model_task_runner_, model_)});
  }
  model_task_runner_->PostTask(FROM_HERE, std::move(on_complete));
  return (model_ != 0) ? LoadModelResult::kSuccess
                       : LoadModelResult::kFailedToLoadLibrary;
}

// static
void OnDeviceModelExecutor::Schedule(uintptr_t context,
                                     std::function<void()>* fn) {
  base::ThreadPool::PostTask(
      FROM_HERE, {base::TaskPriority::USER_BLOCKING, base::MayBlock()},
      base::BindOnce([](std::function<void()> fn) { fn(); }, std::move(*fn)));
}

}  // namespace ml
