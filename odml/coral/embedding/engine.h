// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_EMBEDDING_ENGINE_H_
#define ODML_CORAL_EMBEDDING_ENGINE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/coral/common.h"
#include "odml/coral/embedding/embedding_database.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"

namespace coral {

namespace internal {

// Generates the embedding prompt for an entity.
std::string EntityToEmbeddingPrompt(const mojom::Entity& entity);

// Generates a uninque cache key for an entity.
// All the factors which affect the embedding should be included in the key.
std::optional<std::string> EntityToCacheKey(const mojom::Entity& entity,
                                            const std::string& prompt,
                                            const std::string& model_version);

}  // namespace internal

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

class EmbeddingEngine : public EmbeddingEngineInterface,
                        public odml::SessionStateManagerInterface::Observer {
 public:
  EmbeddingEngine(
      raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
          embedding_service,
      std::unique_ptr<EmbeddingDatabaseFactory> embedding_database_factory,
      odml::SessionStateManagerInterface* session_state_manager);
  ~EmbeddingEngine() = default;

  // EmbeddingEngineInterface overrides.
  void Process(mojom::GroupRequestPtr request,
               EmbeddingCallback callback) override;

  // SessionStateManagerInterface overrides.
  void OnUserLoggedIn(
      const odml::SessionStateManagerInterface::User& user) override;
  void OnUserLoggedOut() override;

 private:
  void EnsureModelLoaded(base::OnceClosure callback);
  void OnModelLoadResult(on_device_model::mojom::LoadModelResult result);
  void OnModelVersionLoaded(const std::string& version);
  void ExecutePendingCallbacks();
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
  // model_ should only be used when state_ is kLoaded because the remote model
  // service only binds the model receiver when model loading succeeds.
  mojo::Remote<embedding_model::mojom::OnDeviceEmbeddingModel> model_;
  ModelLoadState state_ = ModelLoadState::kNew;

  // Callbacks that are queued and waiting for the model to be loaded.
  std::vector<base::OnceClosure> pending_callbacks_;

  // Factory to create an embedding database to cache embedding vectors.
  std::unique_ptr<EmbeddingDatabaseFactory> embedding_database_factory_;

  // The embedding database.
  std::unique_ptr<EmbeddingDatabase> embedding_database_;

  // The version of the loaded embedding model.
  std::string model_version_;

  base::WeakPtrFactory<EmbeddingEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_EMBEDDING_ENGINE_H_
