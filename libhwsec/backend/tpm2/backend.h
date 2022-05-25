// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM2_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trunks/command_transceiver.h>
#include <trunks/trunks_factory.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm2/key_managerment.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"

namespace hwsec {

class BackendTpm2 : public Backend {
 public:
  class StateTpm2 : public State, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<bool> IsReady() override;
    Status Prepare() override;
  };

  class SealingTpm2 : public Sealing, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Seal(
        const OperationPolicySetting& policy,
        const brillo::SecureBlob& unsealed_data) override;
    StatusOr<std::optional<ScopedKey>> PreloadSealedData(
        const OperationPolicy& policy,
        const brillo::Blob& sealed_data) override;
    StatusOr<brillo::SecureBlob> Unseal(const OperationPolicy& policy,
                                        const brillo::Blob& sealed_data,
                                        UnsealOptions options) override;
  };

  class DerivingTpm2 : public Deriving, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Derive(Key key, const brillo::Blob& blob) override;
    StatusOr<brillo::SecureBlob> SecureDerive(
        Key key, const brillo::SecureBlob& blob) override;

   private:
    StatusOr<brillo::SecureBlob> DeriveRsaKey(const KeyTpm2& key_data,
                                              const brillo::SecureBlob& blob);
    StatusOr<brillo::SecureBlob> DeriveEccKey(const KeyTpm2& key_data,
                                              const brillo::SecureBlob& blob);
  };

  class EncryptionTpm2 : public Encryption, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Encrypt(Key key,
                                   const brillo::SecureBlob& plaintext,
                                   EncryptionOptions options) override;
    StatusOr<brillo::SecureBlob> Decrypt(Key key,
                                         const brillo::Blob& ciphertext,
                                         EncryptionOptions options) override;
  };

  class KeyManagermentTpm2 : public KeyManagerment,
                             public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    ~KeyManagermentTpm2();

    StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() override;
    StatusOr<CreateKeyResult> CreateKey(const OperationPolicySetting& policy,
                                        KeyAlgoType key_algo,
                                        CreateKeyOptions options) override;
    StatusOr<ScopedKey> LoadKey(const OperationPolicy& policy,
                                const brillo::Blob& key_blob) override;
    StatusOr<CreateKeyResult> CreateAutoReloadKey(
        const OperationPolicySetting& policy,
        KeyAlgoType key_algo,
        CreateKeyOptions options) override;
    StatusOr<ScopedKey> LoadAutoReloadKey(
        const OperationPolicy& policy, const brillo::Blob& key_blob) override;
    StatusOr<ScopedKey> GetPersistentKey(PersistentKeyType key_type) override;
    StatusOr<brillo::Blob> GetPubkeyHash(Key key) override;
    Status Flush(Key key) override;
    Status ReloadIfPossible(Key key) override;

    StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) override;
    StatusOr<uint32_t> GetKeyHandle(Key key) override;

    StatusOr<std::reference_wrapper<KeyTpm2>> GetKeyData(Key key);

   private:
    StatusOr<CreateKeyResult> CreateRsaKey(const OperationPolicySetting& policy,
                                           const CreateKeyOptions& options,
                                           bool auto_reload);
    StatusOr<CreateKeyResult> CreateSoftwareGenRsaKey(
        const OperationPolicySetting& policy,
        const CreateKeyOptions& options,
        bool auto_reload);
    StatusOr<CreateKeyResult> CreateEccKey(const OperationPolicySetting& policy,
                                           const CreateKeyOptions& options,
                                           bool auto_reload);
    StatusOr<ScopedKey> LoadKeyInternal(
        KeyTpm2::Type key_type,
        uint32_t key_handle,
        std::optional<KeyReloadDataTpm2> reload_data);

    KeyToken current_token_ = 0;
    absl::flat_hash_map<KeyToken, KeyTpm2> key_map_;
    absl::flat_hash_map<PersistentKeyType, KeyToken> persistent_key_map_;
  };

  class ConfigTpm2 : public Config, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<OperationPolicy> ToOperationPolicy(
        const OperationPolicySetting& policy) override;
    Status SetCurrentUser(const std::string& current_user) override;
    StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) override;

    using PcrMap = std::map<uint32_t, std::string>;
    struct TrunksSession {
      using InnerSession = std::variant<std::unique_ptr<trunks::HmacSession>,
                                        std::unique_ptr<trunks::PolicySession>>;
      InnerSession session;
      trunks::AuthorizationDelegate* delegate;
    };
    StatusOr<PcrMap> ToPcrMap(const DeviceConfigs& device_config);
    StatusOr<PcrMap> ToSettingsPcrMap(const DeviceConfigSettings& settings);
    StatusOr<TrunksSession> GetTrunksSession(const OperationPolicy& policy,
                                             bool salted = true,
                                             bool enable_encryption = true);

   private:
    StatusOr<std::string> ReadPcr(uint32_t pcr_index);
  };

  class RandomTpm2 : public Random, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> RandomBlob(size_t size) override;
    StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) override;
  };

  class PinWeaverTpm2 : public PinWeaver, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<uint8_t> GetVersion() override;
    StatusOr<brillo::Blob> SendCommand(const brillo::Blob& command) override;
  };

  BackendTpm2(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm2() override;

  void set_middleware_derivative_for_test(
      MiddlewareDerivative middleware_derivative) {
    middleware_derivative_ = middleware_derivative;
  }

 private:
  // This structure holds all Trunks client objects.
  struct TrunksClientContext {
    trunks::CommandTransceiver& command_transceiver;
    trunks::TrunksFactory& factory;
    std::unique_ptr<trunks::TpmState> tpm_state;
    std::unique_ptr<trunks::TpmUtility> tpm_utility;
  };

  State* GetState() override { return &state_; }
  DAMitigation* GetDAMitigation() override { return nullptr; }
  Storage* GetStorage() override { return nullptr; }
  RoData* GetRoData() override { return nullptr; }
  Sealing* GetSealing() override { return &sealing_; }
  SignatureSealing* GetSignatureSealing() override { return nullptr; }
  Deriving* GetDeriving() override { return &deriving_; }
  Encryption* GetEncryption() override { return &encryption_; }
  Signing* GetSigning() override { return nullptr; }
  KeyManagerment* GetKeyManagerment() override { return &key_managerment_; }
  SessionManagerment* GetSessionManagerment() override { return nullptr; }
  Config* GetConfig() override { return &config_; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return &pinweaver_; }
  Vendor* GetVendor() override { return nullptr; }

  Proxy& proxy_;

  TrunksClientContext trunks_context_;

  StateTpm2 state_{*this};
  SealingTpm2 sealing_{*this};
  DerivingTpm2 deriving_{*this};
  EncryptionTpm2 encryption_{*this};
  KeyManagermentTpm2 key_managerment_{*this};
  ConfigTpm2 config_{*this};
  RandomTpm2 random_{*this};
  PinWeaverTpm2 pinweaver_{*this};

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_BACKEND_H_
