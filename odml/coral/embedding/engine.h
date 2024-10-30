// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_EMBEDDING_ENGINE_H_
#define ODML_CORAL_EMBEDDING_ENGINE_H_

#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "odml/coral/common.h"
#include "odml/coral/embedding/embedding_database.h"
#include "odml/coral/metrics.h"
#include "odml/mojom/coral_service.mojom.h"
#include "odml/mojom/embedding_model.mojom.h"
#include "odml/mojom/on_device_model.mojom.h"
#include "odml/session_state_manager/session_state_manager.h"

namespace coral {

namespace internal {

inline constexpr base::TimeDelta kEmbeddingDatabaseSyncPeriod =
    base::Minutes(10);

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

  // Claim resources necessary for `Process`, like downloading from dlc, loading
  // model etc. It is not necessary to call this before `Process`, but the first
  // `Process` will take longer without calling `PrepareResource` first.
  virtual void PrepareResource() {}

  using EmbeddingCallback = base::OnceCallback<void(
      mojom::GroupRequestPtr, CoralResult<EmbeddingResponse>)>;
  virtual void Process(mojom::GroupRequestPtr request,
                       EmbeddingCallback callback) = 0;
};

class EmbeddingEngine : public EmbeddingEngineInterface,
                        public odml::SessionStateManagerInterface::Observer {
 public:
  EmbeddingEngine(
      raw_ref<CoralMetrics> metrics,
      raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
          embedding_service,
      std::unique_ptr<EmbeddingDatabaseFactory> embedding_database_factory,
      odml::SessionStateManagerInterface* session_state_manager);
  ~EmbeddingEngine() = default;

  // EmbeddingEngineInterface overrides.
  void PrepareResource() override;
  void Process(mojom::GroupRequestPtr request,
               EmbeddingCallback callback) override;

  // SessionStateManagerInterface overrides.
  void OnUserLoggedIn(
      const odml::SessionStateManagerInterface::User& user) override;
  void OnUserLoggedOut() override;

 private:
  void EnsureModelLoaded(base::OnceClosure callback);
  void OnModelLoadResult(base::OnceClosure callback,
                         on_device_model::mojom::LoadModelResult result);
  void OnModelVersionLoaded(base::OnceClosure callback,
                            const std::string& version);
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

  void SyncDatabase();

  // Report metrics and return to callback.
  void HandleProcessResult(EmbeddingCallback callback,
                           mojom::GroupRequestPtr request,
                           CoralResult<EmbeddingResponse> result);

  void OnProcessCompleted();

  const raw_ref<CoralMetrics> metrics_;

  const raw_ref<embedding_model::mojom::OnDeviceEmbeddingModelService>
      embedding_service_;
  // `model_` should only be used after a successful LoadModelResult is received
  // because on device service only binds the model receiver when model loading
  // succeeds.
  mojo::Remote<embedding_model::mojom::OnDeviceEmbeddingModel> model_;

  // Callbacks that are queued and waiting for the previous request to
  // complete, and flag to indicate that a request is being processed.
  std::queue<base::OnceClosure> pending_callbacks_;
  bool is_processing_ = false;

  // Factory to create an embedding database to cache embedding vectors.
  std::unique_ptr<EmbeddingDatabaseFactory> embedding_database_factory_;

  // The embedding database.
  std::unique_ptr<EmbeddingDatabaseInterface> embedding_database_;

  // The version of the loaded embedding model.
  std::string model_version_;

  // The timer to sync database to disk periodically.
  base::RepeatingTimer sync_db_timer_;

  base::WeakPtrFactory<EmbeddingEngine> weak_ptr_factory_{this};
};

}  // namespace coral

#endif  // ODML_CORAL_EMBEDDING_ENGINE_H_
