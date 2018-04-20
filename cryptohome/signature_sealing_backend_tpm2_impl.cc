// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/signature_sealing_backend_tpm2_impl.h"

#include <stdint.h>

#include <cstring>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/numerics/safe_conversions.h>
#include <base/memory/ptr_util.h>
#include <base/threading/thread_checker.h>
#include <brillo/secure_blob.h>
#include <trunks/error_codes.h>
#include <trunks/policy_session.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory_impl.h>
#include <trunks/hmac_session.h>
#include <trunks/tpm_utility.h>
#include <trunks/authorization_delegate.h>

#include "cryptohome/tpm2_impl.h"
#include "signature_sealed_data.pb.h"  // NOLINT(build/include)

using brillo::SecureBlob;
using trunks::GetErrorString;
using trunks::TPM_ALG_ID;
using trunks::TPM_ALG_NULL;
using trunks::TPM_RC;
using trunks::TPM_RC_SUCCESS;

namespace cryptohome {

namespace {

// Size, in bytes, of the secret value that is generated by
// SignatureSealingBackendTpm2Impl::CreateSealedSecret().
constexpr int kSecretSizeBytes = 32;

class UnsealingSessionTpm2Impl final
    : public SignatureSealingBackend::UnsealingSession {
 public:
  UnsealingSessionTpm2Impl(
      Tpm2Impl* tpm,
      Tpm2Impl::TrunksClientContext* trunks,
      const SecureBlob& srk_wrapped_secret,
      const SecureBlob& public_key_spki_der,
      Algorithm algorithm,
      TPM_ALG_ID scheme,
      TPM_ALG_ID hash_alg,
      const std::vector<uint32_t>& bound_pcrs,
      std::unique_ptr<trunks::PolicySession> policy_session,
      const SecureBlob& policy_session_tpm_nonce);
  ~UnsealingSessionTpm2Impl() override;

  // UnsealingSession:
  Algorithm GetChallengeAlgorithm() override;
  SecureBlob GetChallengeValue() override;
  bool Unseal(const SecureBlob& signed_challenge_value,
              SecureBlob* unsealed_value) override;

 private:
  // Unowned.
  Tpm2Impl* const tpm_;
  // Unowned.
  Tpm2Impl::TrunksClientContext* const trunks_;
  const SecureBlob srk_wrapped_secret_;
  const SecureBlob public_key_spki_der_;
  const Algorithm algorithm_;
  const TPM_ALG_ID scheme_;
  const TPM_ALG_ID hash_alg_;
  const std::vector<uint32_t> bound_pcrs_;
  const std::unique_ptr<trunks::PolicySession> policy_session_;
  const SecureBlob policy_session_tpm_nonce_;
  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(UnsealingSessionTpm2Impl);
};

// Obtains the TPM 2.0 signature scheme and hashing algorithms that correspond
// to the provided challenge signature algorithm.
bool GetAlgIdsByAlgorithm(SignatureSealingBackend::Algorithm algorithm,
                          TPM_ALG_ID* scheme,
                          TPM_ALG_ID* hash_alg) {
  using Algorithm = SignatureSealingBackend::Algorithm;
  switch (algorithm) {
    case Algorithm::kRsassaPkcs1V15Sha1:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA1;
      return true;
    case Algorithm::kRsassaPkcs1V15Sha256:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA256;
      return true;
    case Algorithm::kRsassaPkcs1V15Sha384:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA384;
      return true;
    case Algorithm::kRsassaPkcs1V15Sha512:
      *scheme = trunks::TPM_ALG_RSASSA;
      *hash_alg = trunks::TPM_ALG_SHA512;
      return true;
    default:
      NOTREACHED();
      return false;
  }
}

UnsealingSessionTpm2Impl::UnsealingSessionTpm2Impl(
    Tpm2Impl* tpm,
    Tpm2Impl::TrunksClientContext* trunks,
    const SecureBlob& srk_wrapped_secret,
    const SecureBlob& public_key_spki_der,
    Algorithm algorithm,
    TPM_ALG_ID scheme,
    TPM_ALG_ID hash_alg,
    const std::vector<uint32_t>& bound_pcrs,
    std::unique_ptr<trunks::PolicySession> policy_session,
    const SecureBlob& policy_session_tpm_nonce)
    : tpm_(tpm),
      trunks_(trunks),
      srk_wrapped_secret_(srk_wrapped_secret),
      public_key_spki_der_(public_key_spki_der),
      algorithm_(algorithm),
      scheme_(scheme),
      hash_alg_(hash_alg),
      bound_pcrs_(bound_pcrs),
      policy_session_(std::move(policy_session)),
      policy_session_tpm_nonce_(policy_session_tpm_nonce) {}

UnsealingSessionTpm2Impl::~UnsealingSessionTpm2Impl() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

SignatureSealingBackend::Algorithm
UnsealingSessionTpm2Impl::GetChallengeAlgorithm() {
  DCHECK(thread_checker_.CalledOnValidThread());
  return algorithm_;
}

SecureBlob UnsealingSessionTpm2Impl::GetChallengeValue() {
  DCHECK(thread_checker_.CalledOnValidThread());
  const SecureBlob expiration_blob(4, 0);  // zero expiration (4-byte integer)
  return SecureBlob::Combine(policy_session_tpm_nonce_, expiration_blob);
}

bool UnsealingSessionTpm2Impl::Unseal(const SecureBlob& signed_challenge_value,
                                      SecureBlob* unsealed_value) {
  DCHECK(thread_checker_.CalledOnValidThread());
  // Start a TPM authorization session.
  std::unique_ptr<trunks::HmacSession> session =
      trunks_->factory->GetHmacSession();
  TPM_RC tpm_result = trunks_->tpm_utility->StartSession(session.get());
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session.";
    return false;
  }
  // Load the protection public key onto the TPM.
  ScopedKeyHandle key_handle;
  if (!tpm_->LoadPublicKeyFromSpki(
          public_key_spki_der_, trunks::TpmUtility::kSignKey, scheme_,
          hash_alg_, session->GetDelegate(), &key_handle)) {
    LOG(ERROR) << "Error loading protection key";
    return false;
  }
  std::string key_name;
  tpm_result = trunks_->tpm_utility->GetKeyName(key_handle.value(), &key_name);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to get key name";
    return false;
  }
  // Update the policy with restricting to selected PCRs.
  // TODO(emaxx): Replace the loop with a single call to PolicyPCR() once the
  // trunks API is changed to support that.
  for (uint32_t pcr_index : bound_pcrs_) {
    tpm_result = policy_session_->PolicyPCR(pcr_index, "" /* pcr_value */);
    if (tpm_result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error restricting policy to PCR: "
                 << GetErrorString(tpm_result);
      return false;
    }
  }
  // Update the policy with the signature.
  trunks::TPMT_SIGNATURE signature;
  memset(&signature, 0, sizeof(trunks::TPMT_SIGNATURE));
  signature.sig_alg = scheme_;
  signature.signature.rsassa.hash = hash_alg_;
  signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA(signed_challenge_value.to_string());
  tpm_result = policy_session_->PolicySigned(
      key_handle.value(), key_name, policy_session_tpm_nonce_.to_string(),
      std::string() /* cp_hash */, std::string() /* policy_ref */,
      0 /* expiration */, signature, session->GetDelegate());
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to signature with the public key: "
               << GetErrorString(tpm_result);
    return false;
  }
  // Obtain the resulting policy digest.
  std::string policy_digest;
  tpm_result = policy_session_->GetDigest(&policy_digest);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(tpm_result);
    return false;
  }
  // Unseal the secret value.
  std::string unsealed_value_string;
  tpm_result = trunks_->tpm_utility->UnsealData(srk_wrapped_secret_.to_string(),
                                                policy_session_->GetDelegate(),
                                                &unsealed_value_string);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error unsealing object: " << GetErrorString(tpm_result);
    return false;
  }
  *unsealed_value = SecureBlob(unsealed_value_string);
  return true;
}

}  // namespace

SignatureSealingBackendTpm2Impl::SignatureSealingBackendTpm2Impl(Tpm2Impl* tpm)
    : tpm_(tpm) {}

SignatureSealingBackendTpm2Impl::~SignatureSealingBackendTpm2Impl() = default;

bool SignatureSealingBackendTpm2Impl::CreateSealedSecret(
    const SecureBlob& public_key_spki_der,
    const std::vector<Algorithm>& key_algorithms,
    const std::map<uint32_t, SecureBlob>& pcr_values,
    const SecureBlob& /* delegate_blob */,
    const SecureBlob& /* delegate_secret */,
    SignatureSealedData* sealed_secret_data) {
  // Choose the algorithm. Respect the input's algorithm prioritization, with
  // the exception of considering SHA-1 as the least preferred option.
  TPM_ALG_ID scheme = TPM_ALG_NULL;
  TPM_ALG_ID hash_alg = TPM_ALG_NULL;
  for (auto algorithm : key_algorithms) {
    TPM_ALG_ID current_scheme = TPM_ALG_NULL;
    TPM_ALG_ID current_hash_alg = TPM_ALG_NULL;
    if (GetAlgIdsByAlgorithm(algorithm, &current_scheme, &current_hash_alg)) {
      scheme = current_scheme;
      hash_alg = current_hash_alg;
      if (hash_alg != trunks::TPM_ALG_SHA1)
        break;
    }
  }
  if (scheme == TPM_ALG_NULL) {
    LOG(ERROR) << "Error choosing the signature algorithm";
    return false;
  }
  // Start a TPM authorization session.
  Tpm2Impl::TrunksClientContext* trunks = nullptr;
  if (!tpm_->GetTrunksContext(&trunks)) {
    LOG(ERROR) << "Error getting trunks context";
    return false;
  }
  std::unique_ptr<trunks::HmacSession> session =
      trunks->factory->GetHmacSession();
  TPM_RC tpm_result = trunks->tpm_utility->StartSession(session.get());
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting hmac session";
    return false;
  }
  // Load the protection public key onto the TPM.
  ScopedKeyHandle key_handle;
  if (!tpm_->LoadPublicKeyFromSpki(
          public_key_spki_der, trunks::TpmUtility::kSignKey, scheme, hash_alg,
          session->GetDelegate(), &key_handle)) {
    LOG(ERROR) << "Error loading protection key";
    return false;
  }
  std::string key_name;
  tpm_result = trunks->tpm_utility->GetKeyName(key_handle.value(), &key_name);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to get key name";
    return false;
  }
  // Start a trial policy session for sealing the secret value.
  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetTrialSession();
  tpm_result = policy_session->StartUnboundSession(false);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting a trial session: "
               << GetErrorString(tpm_result);
    return false;
  }
  // Update the policy with restricting to selected PCRs.
  // TODO(emaxx): Replace the loop with a single call to PolicyPCR() once the
  // trunks API is changed to support that.
  for (const auto& pcr_index_and_value : pcr_values) {
    tpm_result = policy_session->PolicyPCR(
        pcr_index_and_value.first, pcr_index_and_value.second.to_string());
    if (tpm_result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error restricting policy to PCR: "
                 << GetErrorString(tpm_result);
      return false;
    }
  }
  // Update the policy with an empty signature that refers to the public key.
  trunks::TPMT_SIGNATURE signature;
  memset(&signature, 0, sizeof(trunks::TPMT_SIGNATURE));
  signature.sig_alg = scheme;
  signature.signature.rsassa.hash = hash_alg;
  signature.signature.rsassa.sig =
      trunks::Make_TPM2B_PUBLIC_KEY_RSA(std::string());
  tpm_result = policy_session->PolicySigned(
      key_handle.value(), key_name, std::string() /* nonce */,
      std::string() /* cp_hash */, std::string() /* policy_ref */,
      0 /* expiration */, signature, session->GetDelegate());
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error restricting policy to signature with the public key: "
               << GetErrorString(tpm_result);
    return false;
  }
  // Obtain the resulting policy digest.
  std::string policy_digest;
  tpm_result = policy_session->GetDigest(&policy_digest);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting policy digest: " << GetErrorString(tpm_result);
    return false;
  }
  // Generate the secret value randomly.
  SecureBlob secret_value;
  if (!tpm_->GetRandomDataSecureBlob(kSecretSizeBytes, &secret_value)) {
    LOG(ERROR) << "Error generating random secret";
    return false;
  }
  // Seal the secret value.
  std::string sealed_value;
  tpm_result =
      trunks->tpm_utility->SealData(secret_value.to_string(), policy_digest,
                                    session->GetDelegate(), &sealed_value);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error sealing secret data: " << GetErrorString(tpm_result);
    return false;
  }
  // Fill the resulting proto with data required for unsealing.
  sealed_secret_data->Clear();
  SignatureSealedData_Tpm2PolicySignedData* const sealed_data_contents =
      sealed_secret_data->mutable_tpm2_policy_signed_data();
  sealed_data_contents->set_public_key_spki_der(
      public_key_spki_der.to_string());
  sealed_data_contents->set_srk_wrapped_secret(sealed_value);
  sealed_data_contents->set_scheme(scheme);
  sealed_data_contents->set_hash_alg(hash_alg);
  for (const auto& pcr_index_and_value : pcr_values)
    sealed_data_contents->add_bound_pcr(pcr_index_and_value.first);
  return true;
}

std::unique_ptr<SignatureSealingBackend::UnsealingSession>
SignatureSealingBackendTpm2Impl::CreateUnsealingSession(
    const SignatureSealedData& sealed_secret_data,
    const SecureBlob& public_key_spki_der,
    const std::vector<Algorithm>& key_algorithms,
    const SecureBlob& /* delegate_blob */,
    const SecureBlob& /* delegate_secret */) {
  // Validate the parameters.
  if (!sealed_secret_data.has_tpm2_policy_signed_data()) {
    LOG(ERROR) << "Error: sealed data is empty or uses unexpected method";
    return nullptr;
  }
  const SignatureSealedData_Tpm2PolicySignedData& sealed_data_contents =
      sealed_secret_data.tpm2_policy_signed_data();
  if (sealed_data_contents.public_key_spki_der() !=
      public_key_spki_der.to_string()) {
    LOG(ERROR) << "Error: wrong subject public key info";
    return nullptr;
  }
  if (!base::IsValueInRangeForNumericType<TPM_ALG_ID>(
          sealed_data_contents.scheme())) {
    LOG(ERROR) << "Error parsing signature scheme";
    return nullptr;
  }
  const TPM_ALG_ID scheme =
      static_cast<TPM_ALG_ID>(sealed_data_contents.scheme());
  if (!base::IsValueInRangeForNumericType<TPM_ALG_ID>(
          sealed_data_contents.hash_alg())) {
    LOG(ERROR) << "Error parsing signature hash algorithm";
    return nullptr;
  }
  const TPM_ALG_ID hash_alg =
      static_cast<TPM_ALG_ID>(sealed_data_contents.hash_alg());
  std::unique_ptr<Algorithm> chosen_algorithm;
  for (auto algorithm : key_algorithms) {
    TPM_ALG_ID current_scheme = TPM_ALG_NULL;
    TPM_ALG_ID current_hash_alg = TPM_ALG_NULL;
    if (GetAlgIdsByAlgorithm(algorithm, &current_scheme, &current_hash_alg) &&
        current_scheme == scheme && current_hash_alg == hash_alg) {
      chosen_algorithm = std::make_unique<Algorithm>(algorithm);
      break;
    }
  }
  if (!chosen_algorithm) {
    LOG(ERROR) << "Error: key doesn't support required algorithm";
    return nullptr;
  }
  // Obtain the trunks context to be used for the whole unsealing session.
  Tpm2Impl::TrunksClientContext* trunks = nullptr;
  if (!tpm_->GetTrunksContext(&trunks))
    return nullptr;
  // Start a policy session that will be used for obtaining the TPM nonce and
  // unsealing the secret value.
  std::unique_ptr<trunks::PolicySession> policy_session =
      trunks->factory->GetPolicySession();
  TPM_RC tpm_result = policy_session->StartUnboundSession(false);
  if (tpm_result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error starting a policy session: "
               << GetErrorString(tpm_result);
    return nullptr;
  }
  // Obtain the TPM nonce.
  std::string tpm_nonce;
  if (!policy_session->GetDelegate()->GetTpmNonce(&tpm_nonce)) {
    LOG(ERROR) << "Error obtaining TPM nonce";
    return nullptr;
  }
  // Create the unsealing session that will keep the required state.
  std::vector<uint32_t> bound_pcrs;
  for (int index = 0; index < sealed_data_contents.bound_pcr_size(); ++index)
    bound_pcrs.push_back(sealed_data_contents.bound_pcr(index));
  return std::make_unique<UnsealingSessionTpm2Impl>(
      tpm_, trunks, SecureBlob(sealed_data_contents.srk_wrapped_secret()),
      public_key_spki_der, *chosen_algorithm, scheme, hash_alg, bound_pcrs,
      std::move(policy_session), SecureBlob(tpm_nonce));
}

}  // namespace cryptohome
