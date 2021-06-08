// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm_bound_to_pcr_auth_block.h"

#include <map>
#include <string>

#include <base/check.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/cryptohome_key_loader.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_auth_block_utils.h"
#include "cryptohome/vault_keyset.pb.h"

namespace cryptohome {

TpmBoundToPcrAuthBlock::TpmBoundToPcrAuthBlock(
    Tpm* tpm, CryptohomeKeyLoader* cryptohome_key_loader)
    : AuthBlock(kTpmBackedPcrBound),
      tpm_(tpm),
      cryptohome_key_loader_(cryptohome_key_loader),
      utils_(tpm, cryptohome_key_loader) {
  CHECK(tpm != nullptr);
  CHECK(cryptohome_key_loader != nullptr);
}

base::Optional<AuthBlockState> TpmBoundToPcrAuthBlock::Create(
    const AuthInput& user_input, KeyBlobs* key_blobs, CryptoError* error) {
  const brillo::SecureBlob& vault_key = user_input.user_input.value();
  const brillo::SecureBlob& salt = user_input.salt.value();
  const std::string& obfuscated_username =
      user_input.obfuscated_username.value();

  // If the cryptohome key isn't loaded, try to load it.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    cryptohome_key_loader_->Init();

  // If the key still isn't loaded, fail the operation.
  if (!cryptohome_key_loader_->HasCryptohomeKey())
    return base::nullopt;

  const auto vkk_key = CreateSecureRandomBlob(kDefaultAesKeySize);
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  brillo::SecureBlob vkk_iv(kAesBlockSize);
  if (!CryptoLib::DeriveSecretsScrypt(vault_key, salt, {&pass_blob, &vkk_iv}))
    return base::nullopt;

  std::map<uint32_t, std::string> default_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
  std::map<uint32_t, std::string> extended_pcr_map =
      tpm_->GetPcrMap(obfuscated_username, true /* use_extended_pcr */);

  // Encrypt the VKK using the TPM and the user's passkey. The output is two
  // encrypted blobs, sealed to PCR in |tpm_key| and |extended_tpm_key|,
  // which are stored in the serialized vault keyset.
  brillo::SecureBlob tpm_key;
  if (tpm_->SealToPcrWithAuthorization(
          cryptohome_key_loader_->GetCryptohomeKey(), vkk_key, pass_blob,
          default_pcr_map, &tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds.";
    return base::nullopt;
  }

  brillo::SecureBlob extended_tpm_key;
  if (tpm_->SealToPcrWithAuthorization(
          cryptohome_key_loader_->GetCryptohomeKey(), vkk_key, pass_blob,
          extended_pcr_map, &extended_tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds for extended PCR.";
    return base::nullopt;
  }

  AuthBlockState auth_block_state;
  AuthBlockState::TpmBoundToPcrAuthBlockState* auth_state =
      auth_block_state.mutable_tpm_bound_to_pcr_state();

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  brillo::SecureBlob pub_key_hash;
  if (tpm_->GetPublicKeyHash(cryptohome_key_loader_->GetCryptohomeKey(),
                             &pub_key_hash) == Tpm::kTpmRetryNone) {
    auth_state->set_tpm_public_key_hash(pub_key_hash.data(),
                                        pub_key_hash.size());
  } else {
    LOG(ERROR) << "Failed to get the TPM public key hash";
  }

  auth_state->set_scrypt_derived(true);
  auth_state->set_tpm_key(tpm_key.data(), tpm_key.size());
  auth_state->set_extended_tpm_key(extended_tpm_key.data(),
                                   extended_tpm_key.size());

  // Pass back the vkk_key and vkk_iv so the generic secret wrapping can use it.
  key_blobs->vkk_key = vkk_key;
  // Note that one might expect the IV to be part of the AuthBlockState. But
  // since it's taken from the scrypt output, it's actually created by the auth
  // block, not used to initialize the auth block.
  key_blobs->vkk_iv = vkk_iv;
  key_blobs->chaps_iv = vkk_iv;

  return auth_block_state;
}

bool TpmBoundToPcrAuthBlock::Derive(const AuthInput& auth_input,
                                    const AuthBlockState& state,
                                    KeyBlobs* key_out_data,
                                    CryptoError* error) {
  if (!state.has_tpm_bound_to_pcr_state()) {
    DLOG(FATAL) << "Called with an invalid auth block state";
    return false;
  }

  const AuthBlockState::TpmBoundToPcrAuthBlockState& tpm_state =
      state.tpm_bound_to_pcr_state();

  if (!tpm_state.scrypt_derived()) {
    LOG(ERROR) << "All TpmBoundtoPcr operations should be scrypt derived.";
    return false;
  }

  brillo::SecureBlob tpm_public_key_hash;
  if (tpm_state.has_tpm_public_key_hash()) {
    tpm_public_key_hash.assign(tpm_state.tpm_public_key_hash().begin(),
                               tpm_state.tpm_public_key_hash().end());
  }

  if (!utils_.CheckTPMReadiness(tpm_state.has_tpm_key(),
                                tpm_state.has_tpm_public_key_hash(),
                                tpm_public_key_hash, error)) {
    return false;
  }

  key_out_data->vkk_iv = brillo::SecureBlob(kAesBlockSize);
  key_out_data->vkk_key = brillo::SecureBlob(kDefaultAesKeySize);

  bool locked_to_single_user = auth_input.locked_to_single_user.value_or(false);
  brillo::SecureBlob salt(tpm_state.salt().begin(), tpm_state.salt().end());
  std::string tpm_key_str = locked_to_single_user ? tpm_state.extended_tpm_key()
                                                  : tpm_state.tpm_key();
  brillo::SecureBlob tpm_key(tpm_key_str.begin(), tpm_key_str.end());

  if (!DecryptTpmBoundToPcr(auth_input.user_input.value(), tpm_key, salt, error,
                            &key_out_data->vkk_iv.value(),
                            &key_out_data->vkk_key.value())) {
    return false;
  }

  key_out_data->chaps_iv = key_out_data->vkk_iv;

  if (tpm_state.has_wrapped_reset_seed()) {
    key_out_data->wrapped_reset_seed = brillo::SecureBlob();
    key_out_data->wrapped_reset_seed.value().assign(
        tpm_state.wrapped_reset_seed().begin(),
        tpm_state.wrapped_reset_seed().end());
  }

  if (!tpm_state.has_tpm_public_key_hash() && error) {
    *error = CryptoError::CE_NO_PUBLIC_KEY_HASH;
  }

  return true;
}

bool TpmBoundToPcrAuthBlock::DecryptTpmBoundToPcr(
    const brillo::SecureBlob& vault_key,
    const brillo::SecureBlob& tpm_key,
    const brillo::SecureBlob& salt,
    CryptoError* error,
    brillo::SecureBlob* vkk_iv,
    brillo::SecureBlob* vkk_key) const {
  brillo::SecureBlob pass_blob(kDefaultPassBlobSize);
  if (!CryptoLib::DeriveSecretsScrypt(vault_key, salt, {&pass_blob, vkk_iv})) {
    LOG(ERROR) << "scrypt derivation failed";
    return false;
  }

  Tpm::TpmRetryAction retry_action;
  for (int i = 0; i < kTpmDecryptMaxRetries; ++i) {
    std::map<uint32_t, std::string> pcr_map({{kTpmSingleUserPCR, ""}});
    retry_action = tpm_->UnsealWithAuthorization(
        cryptohome_key_loader_->GetCryptohomeKey(), tpm_key, pass_blob, pcr_map,
        vkk_key);

    if (retry_action == Tpm::kTpmRetryNone)
      return true;

    if (!TpmAuthBlockUtils::TpmErrorIsRetriable(retry_action))
      break;

    // If the error is retriable, reload the key first.
    if (!cryptohome_key_loader_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      break;
    }
  }

  LOG(ERROR) << "Failed to unwrap VKK with creds.";
  *error = TpmAuthBlockUtils::TpmErrorToCrypto(retry_action);
  return false;
}

}  // namespace cryptohome
