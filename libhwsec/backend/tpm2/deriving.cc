// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/backend.h"

#include <functional>

#include <base/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/openssl_utility.h>
#include <trunks/tpm_utility.h>

#include "libhwsec/error/elliptic_curve_error.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"

using brillo::Blob;
using brillo::SecureBlob;
using hwsec_foundation::CreateBigNumContext;
using hwsec_foundation::EllipticCurve;
using hwsec_foundation::ScopedBN_CTX;
using hwsec_foundation::SecureBlobToBigNum;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

constexpr uint32_t kDefaultTpmRsaModulusSize = 2048;

constexpr int kMinDeriveBlobSize = 32;

constexpr EllipticCurve::CurveType kDefaultCurve =
    EllipticCurve::CurveType::kPrime256;

hwsec::StatusOr<trunks::TPMS_ECC_POINT> DeriveTpmEccPointFromSeed(
    const SecureBlob& seed) {
  // Generate an ECC private key (scalar) based on the seed.
  crypto::ScopedBIGNUM private_key = SecureBlobToBigNum(Sha256(seed));

  ScopedBN_CTX context = CreateBigNumContext();
  if (!context.get()) {
    return MakeStatus<TPMError>("Failed to allocate BN_CTX structure",
                                TPMRetryAction::kNoRetry);
  }

  std::optional<EllipticCurve> ec =
      EllipticCurve::Create(kDefaultCurve, context.get());
  if (!ec) {
    return MakeStatus<TPMError>("Failed to create EllipticCurve",
                                TPMRetryAction::kNoRetry);
  }

  if (!ec->IsScalarValid(*private_key)) {
    // Generate another blob may resolve this issue.
    return MakeStatus<EllipticCurveError>(
        EllipticCurveErrorCode::kScalarOutOfRange);
  }

  crypto::ScopedEC_POINT public_point =
      ec->MultiplyWithGenerator(*private_key, context.get());

  if (!public_point) {
    return MakeStatus<TPMError>("Failed to multiply with generator",
                                TPMRetryAction::kNoRetry);
  }

  trunks::TPMS_ECC_POINT out_point;

  if (!trunks::OpensslToTpmEccPoint(*ec->GetGroup(), *public_point,
                                    ec->AffineCoordinateSizeInBytes(),
                                    &out_point)) {
    return MakeStatus<TPMError>("Error converting OpenSSL to TPM ECC point",
                                TPMRetryAction::kNoRetry);
  }

  return out_point;
}

}  // namespace

using DerivingTpm2 = BackendTpm2::DerivingTpm2;

StatusOr<Blob> DerivingTpm2::Derive(Key key, const Blob& blob) {
  ASSIGN_OR_RETURN(
      const SecureBlob& result,
      SecureDerive(std::move(key), SecureBlob(blob.begin(), blob.end())),
      _.WithStatus<TPMError>("Failed to derive secure blob"));
  return Blob(result.begin(), result.end());
}

StatusOr<SecureBlob> DerivingTpm2::SecureDerive(Key key,
                                                const SecureBlob& blob) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data,
                   backend_.key_management_.GetKeyData(key));

  switch (key_data.cache.public_area.type) {
    case trunks::TPM_ALG_RSA:
      return DeriveRsaKey(key_data, blob);

    case trunks::TPM_ALG_ECC:
      return DeriveEccKey(key_data, blob);

    default:
      return MakeStatus<TPMError>("Unknown algorithm",
                                  TPMRetryAction::kNoRetry);
  }
}

StatusOr<SecureBlob> DerivingTpm2::DeriveRsaKey(const KeyTpm2& key_data,
                                                const SecureBlob& blob) {
  if (blob.size() != kDefaultTpmRsaModulusSize / 8) {
    return MakeStatus<TPMError>(
        "Unexpected blob size: " + std::to_string(blob.size()),
        TPMRetryAction::kNoRetry);
  }

  TrunksClientContext& context = backend_.trunks_context_;

  // To guarantee that pass_blob is lower that public key modulus, just set the
  // first byte to 0.
  std::string value_to_decrypt = blob.to_string();
  value_to_decrypt[0] = 0;

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_value_to_decrypt(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(value_to_decrypt)));

  std::string decrypted_value;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->AsymmetricDecrypt(
          key_data.key_handle, trunks::TPM_ALG_NULL, trunks::TPM_ALG_NULL,
          value_to_decrypt, delegate.get(), &decrypted_value)))
      .WithStatus<TPMError>("Failed to decrypt blob");

  return Sha256(SecureBlob(decrypted_value));
}

StatusOr<SecureBlob> DerivingTpm2::DeriveEccKey(const KeyTpm2& key_data,
                                                const SecureBlob& blob) {
  if (blob.size() < kMinDeriveBlobSize) {
    return MakeStatus<TPMError>(
        "Unexpected blob size: " + std::to_string(blob.size()),
        TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      const trunks::TPMS_ECC_POINT& ecc_point, DeriveTpmEccPointFromSeed(blob),
      _.WithStatus<TPMError>("Failed to derive TPM ECC point from seed"));

  TrunksClientContext& context = backend_.trunks_context_;

  trunks::TPM2B_ECC_POINT in_point = trunks::Make_TPM2B_ECC_POINT(ecc_point);
  trunks::TPM2B_ECC_POINT z_point;

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->ECDHZGen(
                      key_data.key_handle, in_point, delegate.get(), &z_point)))
      .WithStatus<TPMError>("Failed to ECDH ZGen");

  return Sha256(SecureBlob(StringFrom_TPM2B_ECC_PARAMETER(z_point.point.x)));
}

}  // namespace hwsec
