// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/tflite_model_runner.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <tensorflow/lite/context.h>
#include <tensorflow/lite/delegates/gpu/delegate.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>

namespace embedding_model {

namespace {

int ComputeSizeFromDims(const TfLiteIntArray& dims) {
  int result = 1;
  for (int i = 0; i < dims.size; ++i) {
    result *= dims.data[i];
  }
  return result;
}

using FormatForEmbeddingFunction = std::optional<std::string> (*)(
    const std::string&,
    const std::string&,
    const std::unordered_map<std::string, std::string>&);

constexpr char kClusteringTaskType[] = "clustering";
constexpr char kContentKey[] = "content";

}  // namespace

TfliteModelRunner::TfliteModelRunner(
    ModelInfo&& model_info,
    std::unique_ptr<Tokenizer> tokenizer,
    const raw_ref<odml::OdmlShimLoader> shim_loader)
    : shim_loader_(shim_loader),
      model_info_(std::move(model_info)),
      tflite_info_(std::get_if<struct EmbeddingTfliteModelInfo>(
          &model_info_.type_specific_info)),
      tokenizer_(std::move(tokenizer)) {
  CHECK(tflite_info_);
}

void TfliteModelRunner::Load(base::PassKey<ModelHolder> passkey,
                             LoadCallback callback) {
  CHECK(tokenizer_);
  CHECK(!tokenizer_->IsLoaded());
  model_.reset();
  interpreter_.reset();

  callback =
      base::BindOnce(&TfliteModelRunner::LoadFinishWrapper,
                     base::Unretained(this), passkey, std::move(callback));

  if (shim_loader_->IsShimReady()) {
    OnShimFinishLoading(passkey, std::move(callback), true);
    return;
  }

  shim_loader_->EnsureShimReady(
      base::BindOnce(&TfliteModelRunner::OnShimFinishLoading,
                     base::Unretained(this), passkey, std::move(callback)));
}

void TfliteModelRunner::OnShimFinishLoading(base::PassKey<ModelHolder> passkey,
                                            LoadCallback callback,
                                            bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to load the odml-shim";
    std::move(callback).Run(false);
    return;
  }
  base::OnceCallback<void(bool)> cb =
      base::BindOnce(&TfliteModelRunner::OnTokenizerLoadFinish,
                     base::Unretained(this), std::move(callback));
  tokenizer_->Load(passkey, tflite_info_->spm_path, std::move(cb));
}

void TfliteModelRunner::OnTokenizerLoadFinish(LoadCallback callback,
                                              bool success) {
  CHECK(!model_);
  if (!success) {
    LOG(ERROR) << "Failed to load the tokenizer " << tflite_info_->spm_path;
    std::move(callback).Run(false);
    return;
  }
  CHECK(tokenizer_->IsLoaded());

  model_ =
      tflite::FlatBufferModel::BuildFromFile(tflite_info_->tflite_path.c_str());

  if (!model_) {
    LOG(ERROR) << "Failed to load FlatBufferModel "
               << tflite_info_->tflite_path;
    std::move(callback).Run(false);
    return;
  }

  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> interpreter;
  const TfLiteStatus resolve_status =
      tflite::InterpreterBuilder(*model_, resolver)(&interpreter);
  if (resolve_status != kTfLiteOk || !interpreter) {
    LOG(ERROR) << "Could not resolve model ops.";
    std::move(callback).Run(false);
    return;
  }

  // Apply GPU delegate
  {
    TfLiteGpuDelegateOptionsV2 options(TfLiteGpuDelegateOptionsV2Default());
    options.experimental_flags |= TFLITE_GPU_EXPERIMENTAL_FLAGS_CL_ONLY;
    TfLiteDelegate* delegate = TfLiteGpuDelegateV2Create(&options);
    if (!delegate) {
      LOG(ERROR) << "GPU requested but not available.";
      std::move(callback).Run(false);
      return;
    }
    if (interpreter->ModifyGraphWithDelegate(delegate) != kTfLiteOk) {
      LOG(ERROR) << "Could not use GPU delegate.";
      std::move(callback).Run(false);
      return;
    }
  }

  // Allocate memory for tensors.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    LOG(ERROR) << "Could not allocate tensors.";
    std::move(callback).Run(false);
    return;
  }

  interpreter_ = std::move(interpreter);
  LOG(INFO) << "Model loaded " << tflite_info_->tflite_path;
  std::move(callback).Run(true);
}

void TfliteModelRunner::LoadFinishWrapper(base::PassKey<ModelHolder> passkey,
                                          LoadCallback callback,
                                          bool success) {
  if (!success) {
    // Failed, need to cleanup.
    if (tokenizer_->IsLoaded())
      tokenizer_->Unload(passkey);
    interpreter_.reset();
    model_.reset();
  }

  std::move(callback).Run(success);
}

void TfliteModelRunner::Unload(base::PassKey<ModelHolder> passkey,
                               UnloadCallback callback) {
  CHECK(tokenizer_);
  CHECK(tokenizer_->IsLoaded());
  CHECK(model_);
  CHECK(interpreter_);
  tokenizer_->Unload(passkey);
  interpreter_.reset();
  model_.reset();
  std::move(callback).Run();
}

std::string TfliteModelRunner::GetModelVersion() {
  return model_info_.model_version;
}

void TfliteModelRunner::Run(base::PassKey<ModelHolder> passkey,
                            mojom::GenerateEmbeddingRequestPtr request,
                            RunCallback callback) {
  if (!tokenizer_ || !tokenizer_->IsLoaded() || !interpreter_) {
    LOG(ERROR) << "TfliteModelRunner::Run() called while not loaded.";
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  const TfLiteIntArray& input_dims =
      *interpreter_->tensor(tflite_info_->input_node_id)->dims;
  const TfLiteIntArray& output_dims =
      *interpreter_->tensor(tflite_info_->output_node_id)->dims;

  int input_size = ComputeSizeFromDims(input_dims);
  int output_size = ComputeSizeFromDims(output_dims);

  auto format_for_embedding_fn =
      shim_loader_->Get<FormatForEmbeddingFunction>("FormatForEmbedding");
  if (!format_for_embedding_fn) {
    LOG(ERROR) << "No FormatForEmbedding in odml-shim.";
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  std::unordered_map<std::string, std::string> format_params;
  format_params.insert(make_pair(kContentKey, std::move(request->content)));
  std::optional<std::string> input_str = format_for_embedding_fn(
      model_info_.model_version, kClusteringTaskType, format_params);
  if (!input_str) {
    LOG(ERROR) << "Failed to format input for embedding.";
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  // Tokenize
  std::optional<std::vector<int>> token_ids =
      tokenizer_->Tokenize(passkey, std::move(*input_str));
  if (!token_ids.has_value()) {
    // Tokenizing failed.
    LOG(ERROR) << "Failed to tokenize input for embedding.";
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  if (token_ids->size() > input_size) {
    if (request->truncate_input) {
      token_ids->resize(input_size, 0);
    } else {
      std::move(callback).Run(
          mojom::OnDeviceEmbeddingModelInferenceError::kTooLong,
          std::vector<float>());
      return;
    }
  } else if (token_ids->size() < input_size) {
    token_ids->resize(input_size, 0);
  }

  // Populate input
  int* input_ptr = interpreter_->typed_tensor<int>(tflite_info_->input_node_id);
  for (int i = 0; i < input_size; i++) {
    input_ptr[i] = (*token_ids)[i];
  }

  // Run the embedding model.
  auto ret = interpreter_->Invoke();
  if (ret != kTfLiteOk) {
    LOG(ERROR) << "Tflite graph Invoke() failed unexpectedly.";
    std::move(callback).Run(
        mojom::OnDeviceEmbeddingModelInferenceError::kInternal,
        std::vector<float>());
    return;
  }

  // Extract the output.
  std::vector<float> output;
  float* output_ptr =
      interpreter_->typed_tensor<float>(tflite_info_->output_node_id);
  for (int i = 0; i < output_size; i++) {
    output.push_back(output_ptr[i]);
  }

  std::move(callback).Run(mojom::OnDeviceEmbeddingModelInferenceError::kSuccess,
                          output);
}

}  // namespace embedding_model
