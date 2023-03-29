// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/auth_blocks/pin_weaver_auth_block.h"

#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <brillo/secure_blob.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/hmac.h>
#include <libhwsec-foundation/crypto/scrypt.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>

#include "cryptohome/auth_blocks/sync_to_async_auth_block_adapter.h"
#include "cryptohome/auth_blocks/tpm_auth_block_utils.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/error/action.h"
#include "cryptohome/error/cryptohome_crypto_error.h"
#include "cryptohome/error/location_utils.h"
#include "cryptohome/error/locations.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state.h"
#include "cryptohome/le_credential_manager.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using ::cryptohome::error::CryptohomeCryptoError;
using ::cryptohome::error::ErrorActionSet;
using ::cryptohome::error::PossibleAction;
using ::cryptohome::error::PrimaryAction;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::DeriveSecretsScrypt;
using ::hwsec_foundation::HmacSha256;
using ::hwsec_foundation::kAesBlockSize;
using ::hwsec_foundation::kDefaultAesKeySize;
using ::hwsec_foundation::Sha256;
using ::hwsec_foundation::status::MakeStatus;
using ::hwsec_foundation::status::OkStatus;
using ::hwsec_foundation::status::StatusChain;

namespace cryptohome {
namespace {

constexpr int kDefaultSecretSize = 32;

void LogLERetCode(int le_error) {
  switch (le_error) {
    case LE_CRED_ERROR_NO_FREE_LABEL:
      LOG(ERROR) << "No free label available in hash tree.";
      break;
    case LE_CRED_ERROR_HASH_TREE:
      LOG(ERROR) << "Hash tree error.";
      break;
  }
}

// String used as vector in HMAC operation to derive vkk_seed from High Entropy
// secret.
constexpr char kHESecretHmacData[] = "vkk_seed";

// Constants used to define delay schedules.
constexpr uint32_t kLockoutAttemptLimit = 5;
constexpr uint32_t kInfiniteDelay = std::numeric_limits<uint32_t>::max();

}  // namespace

const LECredentialManager::DelaySchedule& LockoutDelaySchedule() {
  static base::NoDestructor<LECredentialManager::DelaySchedule> kValue(
      LECredentialManager::DelaySchedule{
          {kLockoutAttemptLimit, kInfiniteDelay},
      });
  return *kValue;
}

const LECredentialManager::DelaySchedule& PinDelaySchedule() {
  // TODO(b/272566923): finalize the policy.
  static base::NoDestructor<LECredentialManager::DelaySchedule> kValue(
      LECredentialManager::DelaySchedule{
          {4, 30},
          {6, 1 * base::Time::kSecondsPerMinute},
          {9, 10 * base::Time::kSecondsPerMinute},
          {12, 30 * base::Time::kSecondsPerMinute},
          {14, 1 * base::Time::kSecondsPerHour},
          {16, 2 * base::Time::kSecondsPerHour},
          {18, 5 * base::Time::kSecondsPerHour},
          {20, 12 * base::Time::kSecondsPerHour},
      });
  return *kValue;
}

CryptoStatus PinWeaverAuthBlock::IsSupported(Crypto& crypto) {
  DCHECK(crypto.GetHwsec());
  hwsec::StatusOr<bool> is_ready = crypto.GetHwsec()->IsReady();
  if (!is_ready.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockHwsecReadyErrorInIsSupported),
               ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(is_ready).err_status()));
  }
  if (!is_ready.value()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockHwsecNotReadyInIsSupported),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  hwsec::StatusOr<bool> has_pinweaver = crypto.GetHwsec()->IsPinWeaverEnabled();
  if (!has_pinweaver.ok()) {
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockPinWeaverCheckFailInIsSupported))
        .Wrap(TpmAuthBlockUtils::TPMErrorToCryptohomeCryptoError(
            std::move(has_pinweaver).err_status()));
  }
  if (!has_pinweaver.value()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoPinWeaverInIsSupported),
        ErrorActionSet({PossibleAction::kAuth}), CryptoError::CE_OTHER_CRYPTO);
  }

  if (!crypto.le_manager()) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNullLeManagerInIsSupported),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  return OkStatus<CryptohomeCryptoError>();
}

std::unique_ptr<AuthBlock> PinWeaverAuthBlock::New(
    AsyncInitFeatures& features, LECredentialManager* le_manager) {
  if (le_manager) {
    return std::make_unique<SyncToAsyncAuthBlockAdapter>(
        std::make_unique<PinWeaverAuthBlock>(features, le_manager));
  }
  return nullptr;
}

PinWeaverAuthBlock::PinWeaverAuthBlock(AsyncInitFeatures& features,
                                       LECredentialManager* le_manager)
    : SyncAuthBlock(kLowEntropyCredential),
      features_(&features),
      le_manager_(le_manager) {
  CHECK(features_);
  CHECK_NE(le_manager, nullptr);
}

CryptoStatus PinWeaverAuthBlock::Create(const AuthInput& auth_input,
                                        AuthBlockState* auth_block_state,
                                        KeyBlobs* key_blobs) {
  DCHECK(key_blobs);

  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUserInputInCreate),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_input.obfuscated_username.has_value()) {
    LOG(ERROR) << "Missing obfuscated_username";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUsernameInCreate),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_input.reset_secret.has_value() &&
      !auth_input.reset_seed.has_value()) {
    LOG(ERROR) << "Missing reset_secret or reset_seed";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocPinWeaverAuthBlockNoResetSecretOrResetSeedInCreate),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  PinWeaverAuthBlockState pin_auth_state;
  pin_auth_state.reset_salt = auth_input.reset_salt.has_value()
                                  ? auth_input.reset_salt.value()
                                  : CreateSecureRandomBlob(kAesBlockSize);
  brillo::SecureBlob reset_secret;
  if (auth_input.reset_secret.has_value()) {
    // This case be used for USS as we do not have the concept of reset seed and
    // salt there.
    LOG(INFO) << "PinWeaverAuthBlock: ResetSecret from the AuthInput is passed "
                 "to KeyBlobs.";
    reset_secret = auth_input.reset_secret.value();
  } else {
    // At this point we know auth_input reset_seed is set. The expectation is
    // that this branch of code would be deprecated once we move fully to USS
    // world.
    LOG(INFO) << "PinWeaverAuthBlock: ResetSecret is derived from the "
                 "reset_seed and passed to KeyBlobs.";
    reset_secret = HmacSha256(pin_auth_state.reset_salt.value(),
                              auth_input.reset_seed.value());
  }

  brillo::SecureBlob le_secret(kDefaultSecretSize);
  brillo::SecureBlob kdf_skey(kDefaultSecretSize);
  brillo::SecureBlob salt =
      CreateSecureRandomBlob(CRYPTOHOME_DEFAULT_KEY_SALT_SIZE);
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockScryptDeriveFailedInCreate),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  // Create a randomly generated high entropy secret, derive VKKSeed from it,
  // and use that to generate a VKK. The High Entropy secret will be stored in
  // the LECredentialManager, along with the LE secret (which is |le_secret|
  // here).
  brillo::SecureBlob he_secret = CreateSecureRandomBlob(kDefaultSecretSize);

  // Derive the VKK_seed by performing an HMAC on he_secret.
  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));

  // Generate and store random new IVs for file-encryption keys and
  // chaps key encryption.
  const auto fek_iv = CreateSecureRandomBlob(kAesBlockSize);
  const auto chaps_iv = CreateSecureRandomBlob(kAesBlockSize);

  brillo::SecureBlob vkk_key = HmacSha256(kdf_skey, vkk_seed);

  key_blobs->vkk_key = vkk_key;
  key_blobs->vkk_iv = fek_iv;
  key_blobs->chaps_iv = chaps_iv;
  key_blobs->reset_secret = reset_secret;
  // Once we are able to correctly set up the VaultKeyset encryption,
  // store the Low Entropy and High Entropy credential in the
  // LECredentialManager.

  // Use the default delay schedule for now.
  const auto& delay_sched = LockoutDelaySchedule();

  std::vector<hwsec::OperationPolicySetting> policies = {
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = std::nullopt,
                      },
              },
      },
      hwsec::OperationPolicySetting{
          .device_config_settings =
              hwsec::DeviceConfigSettings{
                  .current_user =
                      hwsec::DeviceConfigSettings::CurrentUserSetting{
                          .username = *auth_input.obfuscated_username.value(),
                      },
              },
      },
  };

  uint64_t label;
  LECredStatus ret = le_manager_->InsertCredential(
      policies, le_secret, he_secret, reset_secret, delay_sched,
      /*expiration_delay=*/std::nullopt, &label);
  if (!ret.ok()) {
    LogLERetCode(ret->local_lecred_error());
    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockInsertCredentialFailedInCreate))
        .Wrap(std::move(ret));
  }

  pin_auth_state.le_label = label;
  pin_auth_state.salt = std::move(salt);
  *auth_block_state = AuthBlockState{.state = std::move(pin_auth_state)};
  return OkStatus<CryptohomeCryptoError>();
}

CryptoStatus PinWeaverAuthBlock::Derive(const AuthInput& auth_input,
                                        const AuthBlockState& state,
                                        KeyBlobs* key_blobs) {
  if (!auth_input.user_input.has_value()) {
    LOG(ERROR) << "Missing user_input";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoUserInputInDerive),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  const PinWeaverAuthBlockState* auth_state;
  if (!(auth_state = std::get_if<PinWeaverAuthBlockState>(&state.state))) {
    LOG(ERROR) << "Invalid AuthBlockState";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockInvalidBlockStateInDerive),
        ErrorActionSet(
            {PossibleAction::kDevCheckUnexpectedState, PossibleAction::kAuth}),
        CryptoError::CE_OTHER_CRYPTO);
  }

  brillo::SecureBlob le_secret(kDefaultAesKeySize);
  brillo::SecureBlob kdf_skey(kDefaultAesKeySize);
  if (!auth_state->le_label.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing le_label";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoLabelInDerive),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kAuth, PossibleAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  if (!auth_state->salt.has_value()) {
    LOG(ERROR) << "Invalid PinWeaverAuthBlockState: missing salt";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoSaltInDerive),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState,
                        PossibleAction::kAuth, PossibleAction::kDeleteVault}),
        CryptoError::CE_OTHER_CRYPTO);
  }
  brillo::SecureBlob salt = auth_state->salt.value();
  if (!DeriveSecretsScrypt(auth_input.user_input.value(), salt,
                           {&le_secret, &kdf_skey})) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockDeriveScryptFailedInDerive),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_FATAL);
  }

  key_blobs->reset_secret = brillo::SecureBlob();
  // Note: Yes it is odd to pass the IV from the auth state into the key blobs
  // without performing any operation on the data. However, the fact that the
  // IVs are pre-generated in the VaultKeyset for PinWeaver credentials is an
  // implementation detail. The AuthBlocks are designed to hide those
  // implementation details, so this goes here.
  if (auth_state->chaps_iv.has_value()) {
    key_blobs->chaps_iv = auth_state->chaps_iv.value();
  }
  if (auth_state->fek_iv.has_value()) {
    key_blobs->vkk_iv = auth_state->fek_iv.value();
  }

  // Try to obtain the High Entropy Secret from the LECredentialManager.
  brillo::SecureBlob he_secret;
  LECredStatus ret = le_manager_->CheckCredential(
      auth_state->le_label.value(), le_secret, &he_secret,
      &key_blobs->reset_secret.value());

  if (!ret.ok()) {
    // If the underlying credential is currently locked, include the
    // kLeLockedOut action.
    if (GetLockoutDelay(auth_state->le_label.value()) > 0) {
      // If it is caused by invalid LE secret
      if (ret->local_lecred_error() == LE_CRED_ERROR_INVALID_LE_SECRET) {
        return MakeStatus<CryptohomeCryptoError>(
                   CRYPTOHOME_ERR_LOC(
                       kLocPinWeaverAuthBlockCheckCredLockedInDerive),
                   ErrorActionSet(PrimaryAction::kLeLockedOut),
                   CryptoError::CE_CREDENTIAL_LOCKED)
            .Wrap(std::move(ret));
        // Or the LE node specified by le_label in PinWeaver is under a lockout
        // timer from previous failed attempts.
      } else if (ret->local_lecred_error() == LE_CRED_ERROR_TOO_MANY_ATTEMPTS) {
        return MakeStatus<CryptohomeCryptoError>(
                   CRYPTOHOME_ERR_LOC(
                       kLocPinWeaverAuthBlockCheckCredTPMLockedInDerive),
                   ErrorActionSet(PrimaryAction::kLeLockedOut))
            .Wrap(std::move(ret));
      }
    }

    return MakeStatus<CryptohomeCryptoError>(
               CRYPTOHOME_ERR_LOC(
                   kLocPinWeaverAuthBlockCheckCredFailedInDerive))
        .Wrap(std::move(ret));
  }

  brillo::SecureBlob vkk_seed =
      HmacSha256(he_secret, brillo::BlobFromString(kHESecretHmacData));
  key_blobs->vkk_key = HmacSha256(kdf_skey, vkk_seed);

  return OkStatus<CryptohomeCryptoError>();
}

CryptohomeStatus PinWeaverAuthBlock::PrepareForRemoval(
    const AuthBlockState& auth_block_state) {
  // Read supported_intents only for AuthFactors with a PinWeaver backend.
  auto* state = std::get_if<PinWeaverAuthBlockState>(&auth_block_state.state);
  if (!state) {
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(
            kLocPinWeaverAuthBlockFailedToGetStateFailedInPrepareForRemoval),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_FATAL);
  }

  // Ensure that the AuthFactor has le_label.
  if (!state->le_label.has_value()) {
    LOG(ERROR) << "PinWeaver AuthBlockState does not have le_label";
    return MakeStatus<CryptohomeCryptoError>(
        CRYPTOHOME_ERR_LOC(kLocPinWeaverAuthBlockNoLabelInPrepareForRemoval),
        ErrorActionSet({PossibleAction::kDevCheckUnexpectedState}),
        CryptoError::CE_OTHER_FATAL);
  }
  return le_manager_->RemoveCredential(state->le_label.value());
}

uint32_t PinWeaverAuthBlock::GetLockoutDelay(uint64_t label) {
  LECredStatusOr<uint32_t> delay = le_manager_->GetDelayInSeconds(label);
  if (!delay.ok()) {
    LOG(ERROR)
        << "Failed to obtain the delay in seconds in pinweaver auth block: "
        << std::move(delay).status();
    return 0;
  }

  return delay.value();
}

}  // namespace cryptohome
