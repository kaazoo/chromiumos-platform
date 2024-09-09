// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_EMBEDDING_ENGINE_H_
#define ODML_CORAL_EMBEDDING_ENGINE_H_

#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/coral/common.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"

namespace coral {

using Embedding = std::vector<float>;

struct EmbeddingResponse : public MoveOnly {
  bool operator==(const EmbeddingResponse&) const = default;

  std::vector<Embedding> embeddings;
};

class EmbeddingEngineInterface {
 public:
  virtual ~EmbeddingEngineInterface() = default;

  using EmbeddingCallback = base::OnceCallback<void(
      mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>)>;
  virtual void Process(mojom::GroupRequestPtr request,
                       EmbeddingCallback callback) = 0;
};

class EmbeddingEngine : public EmbeddingEngineInterface {
 public:
  explicit EmbeddingEngine(
      raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
          embedding_service);
  ~EmbeddingEngine() = default;

  // EmbeddingEngineInterface overrides.
  void Process(mojom::GroupRequestPtr request,
               EmbeddingCallback callback) override;

 private:
  void EnsureModelLoaded(base::OnceClosure callback);
  void OnModelLoadResult(base::OnceClosure callback,
                         on_device_model::mojom::LoadModelResult result);
  void DoProcess(mojom::GroupRequestPtr request, EmbeddingCallback callback);
  void ProcessEachPrompt(mojom::GroupRequestPtr request,
                         std::vector<std::string> prompts,
                         EmbeddingResponse response,
                         EmbeddingCallback callback);
  void OnModelOutput(
      mojom::GroupRequestPtr request,
      std::vector<std::string> prompts,
      EmbeddingResponse response,
      EmbeddingCallback callback,
      embedding_model::mojom::OnDeviceEmbeddingModelInferenceError error,
      const std::vector<float>& embedding);

  const raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
      embedding_service_;
  mojo::Remote<embedding_model::mojom::OnDeviceEmbeddingModel> model_;

  base::WeakPtrFactory<EmbeddingEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_EMBEDDING_ENGINE_H_
