// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/crypto/big_num_util.h>
#include <libhwsec-foundation/crypto/elliptic_curve.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include "cryptohome/cryptorecovery/cryptorecovery.pb.h"
#include "cryptohome/cryptorecovery/fake_recovery_mediator_crypto.h"
#include "cryptohome/cryptorecovery/recovery_crypto_fake_tpm_backend_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_hsm_cbor_serialization.h"
#include "cryptohome/cryptorecovery/recovery_crypto_impl.h"
#include "cryptohome/cryptorecovery/recovery_crypto_util.h"

using brillo::SecureBlob;
using hwsec_foundation::BigNumToSecureBlob;
using hwsec_foundation::CreateBigNumContext;
using hwsec_foundation::EllipticCurve;
using hwsec_foundation::ScopedBN_CTX;

namespace cryptohome {
namespace cryptorecovery {

namespace {

constexpr EllipticCurve::CurveType kCurve = EllipticCurve::CurveType::kPrime256;
const char kFakeGaiaAccessToken[] = "fake access token";
const char kFakeRapt[] = "fake rapt";
const char kFakeUserId[] = "fake user id";

SecureBlob GeneratePublicKey() {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context) {
    ADD_FAILURE() << "CreateBigNumContext failed";
    return SecureBlob();
  }
  std::optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  if (!ec) {
    ADD_FAILURE() << "EllipticCurve::Create failed";
    return SecureBlob();
  }
  crypto::ScopedEC_KEY key = ec->GenerateKey(context.get());
  if (!key) {
    ADD_FAILURE() << "GenerateKey failed";
    return SecureBlob();
  }
  SecureBlob result;
  if (!ec->EncodeToSpkiDer(key, &result, context.get())) {
    ADD_FAILURE() << "EncodeToSpkiDer failed";
    return SecureBlob();
  }
  return result;
}

SecureBlob GenerateScalar() {
  ScopedBN_CTX context = CreateBigNumContext();
  if (!context) {
    ADD_FAILURE() << "CreateBigNumContext failed";
    return SecureBlob();
  }
  std::optional<EllipticCurve> ec =
      EllipticCurve::Create(kCurve, context.get());
  if (!ec) {
    ADD_FAILURE() << "EllipticCurve::Create failed";
    return SecureBlob();
  }
  crypto::ScopedBIGNUM random_bn = ec->RandomNonZeroScalar(context.get());
  if (!random_bn) {
    ADD_FAILURE() << "RandomNonZeroScalar failed";
    return SecureBlob();
  }
  SecureBlob result;
  if (!BigNumToSecureBlob(*random_bn, ec->ScalarSizeInBytes(), &result)) {
    ADD_FAILURE() << "BigNumToSecureBlob failed";
    return SecureBlob();
  }
  return result;
}

}  // namespace

class RecoveryCryptoTest : public testing::Test {
 public:
  RecoveryCryptoTest() {
    onboarding_metadata_.cryptohome_user_type = UserType::kGaiaId;
    onboarding_metadata_.cryptohome_user = "fake user id";
    onboarding_metadata_.device_user_id = "Device User ID";
    onboarding_metadata_.board_name = "Board Name";
    onboarding_metadata_.model_name = "Model Name";
    onboarding_metadata_.recovery_id = "Recovery ID";

    AuthClaim auth_claim;
    auth_claim.gaia_access_token = kFakeGaiaAccessToken;
    auth_claim.gaia_reauth_proof_token = kFakeRapt;
    request_metadata_.auth_claim = std::move(auth_claim);
    request_metadata_.requestor_user_id = kFakeUserId;
    request_metadata_.requestor_user_id_type = UserType::kGaiaId;
  }
  ~RecoveryCryptoTest() = default;

  void SetUp() override {
    ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPublicKey(
        &mediator_pub_key_));
    ASSERT_TRUE(FakeRecoveryMediatorCrypto::GetFakeMediatorPrivateKey(
        &mediator_priv_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochPublicKey(&epoch_pub_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochPrivateKey(&epoch_priv_key_));
    ASSERT_TRUE(
        FakeRecoveryMediatorCrypto::GetFakeEpochResponse(&epoch_response_));

    recovery_ = RecoveryCryptoImpl::Create(&recovery_crypto_fake_tpm_backend_);
    ASSERT_TRUE(recovery_);
    mediator_ = FakeRecoveryMediatorCrypto::Create();
    ASSERT_TRUE(mediator_);
  }

 protected:
  void GenerateSecretsAndMediate(SecureBlob* recovery_key,
                                 SecureBlob* destination_share,
                                 SecureBlob* channel_priv_key,
                                 SecureBlob* ephemeral_pub_key,
                                 CryptoRecoveryRpcResponse* response_proto) {
    // Generates HSM payload that would be persisted on a chromebook.
    GenerateHsmPayloadRequest generate_hsm_payload_request(
        {.mediator_pub_key = mediator_pub_key_,
         .onboarding_metadata = onboarding_metadata_,
         .obfuscated_username = ""});
    GenerateHsmPayloadResponse generate_hsm_payload_response;
    EXPECT_TRUE(recovery_->GenerateHsmPayload(generate_hsm_payload_request,
                                              &generate_hsm_payload_response));
    *destination_share =
        generate_hsm_payload_response.encrypted_destination_share;
    *recovery_key = generate_hsm_payload_response.recovery_key;
    *channel_priv_key =
        generate_hsm_payload_response.encrypted_channel_priv_key;

    // Start recovery process.
    CryptoRecoveryRpcRequest recovery_request;
    EXPECT_TRUE(recovery_->GenerateRecoveryRequest(
        generate_hsm_payload_response.hsm_payload, request_metadata_,
        epoch_response_, generate_hsm_payload_response.encrypted_rsa_priv_key,
        generate_hsm_payload_response.encrypted_channel_priv_key,
        generate_hsm_payload_response.channel_pub_key,
        /*obfuscated_username=*/"", &recovery_request, ephemeral_pub_key));

    // Simulates mediation performed by HSM.
    EXPECT_TRUE(mediator_->MediateRequestPayload(
        epoch_pub_key_, epoch_priv_key_, mediator_priv_key_, recovery_request,
        response_proto));
  }

  SecureBlob rsa_pub_key_;
  OnboardingMetadata onboarding_metadata_;
  RequestMetadata request_metadata_;

  cryptorecovery::RecoveryCryptoFakeTpmBackendImpl
      recovery_crypto_fake_tpm_backend_;

  SecureBlob mediator_pub_key_;
  SecureBlob mediator_priv_key_;
  SecureBlob epoch_pub_key_;
  SecureBlob epoch_priv_key_;
  CryptoRecoveryEpochResponse epoch_response_;
  std::unique_ptr<RecoveryCryptoImpl> recovery_;
  std::unique_ptr<FakeRecoveryMediatorCrypto> mediator_;
};

TEST_F(RecoveryCryptoTest, RecoveryTestSuccess) {
  // Generates HSM payload that would be persisted on a chromebook.
  GenerateHsmPayloadRequest generate_hsm_payload_request(
      {.mediator_pub_key = mediator_pub_key_,
       .onboarding_metadata = onboarding_metadata_,
       .obfuscated_username = ""});
  generate_hsm_payload_request.mediator_pub_key = mediator_pub_key_;
  generate_hsm_payload_request.onboarding_metadata = onboarding_metadata_;
  generate_hsm_payload_request.obfuscated_username = "";
  GenerateHsmPayloadResponse generate_hsm_payload_response;
  EXPECT_TRUE(recovery_->GenerateHsmPayload(generate_hsm_payload_request,
                                            &generate_hsm_payload_response));

  // Start recovery process.
  CryptoRecoveryRpcRequest recovery_request;
  SecureBlob ephemeral_pub_key;
  EXPECT_TRUE(recovery_->GenerateRecoveryRequest(
      generate_hsm_payload_response.hsm_payload, request_metadata_,
      epoch_response_, generate_hsm_payload_response.encrypted_rsa_priv_key,
      generate_hsm_payload_response.encrypted_channel_priv_key,
      generate_hsm_payload_response.channel_pub_key, /*obfuscated_username=*/"",
      &recovery_request, &ephemeral_pub_key));

  // Simulates mediation performed by HSM.
  CryptoRecoveryRpcResponse response_proto;
  EXPECT_TRUE(mediator_->MediateRequestPayload(
      epoch_pub_key_, epoch_priv_key_, mediator_priv_key_, recovery_request,
      &response_proto));

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(recovery_->DecryptResponsePayload(
      generate_hsm_payload_response.encrypted_channel_priv_key, epoch_response_,
      response_proto,
      /*obfuscated_username=*/"", &response_plain_text));

  SecureBlob mediated_recovery_key;
  EXPECT_TRUE(recovery_->RecoverDestination(
      response_plain_text.dealer_pub_key, response_plain_text.key_auth_value,
      generate_hsm_payload_response.encrypted_destination_share,
      ephemeral_pub_key, response_plain_text.mediated_point,
      /*obfuscated_username=*/"", &mediated_recovery_key));

  // Checks that cryptohome encryption key generated at enrollment and the
  // one obtained after migration are identical.
  EXPECT_EQ(generate_hsm_payload_response.recovery_key, mediated_recovery_key);
}

TEST_F(RecoveryCryptoTest, GenerateHsmPayloadInvalidMediatorKey) {
  GenerateHsmPayloadRequest generate_hsm_payload_request(
      {.mediator_pub_key = SecureBlob("not a key"),
       .onboarding_metadata = onboarding_metadata_,
       .obfuscated_username = ""});
  GenerateHsmPayloadResponse generate_hsm_payload_response;
  EXPECT_FALSE(recovery_->GenerateHsmPayload(generate_hsm_payload_request,
                                             &generate_hsm_payload_response));
}

TEST_F(RecoveryCryptoTest, MediateWithInvalidEpochPublicKey) {
  // Generates HSM payload that would be persisted on a chromebook.
  GenerateHsmPayloadRequest generate_hsm_payload_request(
      {.mediator_pub_key = mediator_pub_key_,
       .onboarding_metadata = onboarding_metadata_,
       .obfuscated_username = ""});
  GenerateHsmPayloadResponse generate_hsm_payload_response;
  EXPECT_TRUE(recovery_->GenerateHsmPayload(generate_hsm_payload_request,
                                            &generate_hsm_payload_response));

  // Start recovery process.
  CryptoRecoveryRpcRequest recovery_request;
  SecureBlob ephemeral_pub_key;
  EXPECT_TRUE(recovery_->GenerateRecoveryRequest(
      generate_hsm_payload_response.hsm_payload, request_metadata_,
      epoch_response_, generate_hsm_payload_response.encrypted_rsa_priv_key,
      generate_hsm_payload_response.encrypted_channel_priv_key,
      generate_hsm_payload_response.channel_pub_key, /*obfuscated_username=*/"",
      &recovery_request, &ephemeral_pub_key));

  SecureBlob random_key = GeneratePublicKey();

  // Simulates mediation performed by HSM.
  CryptoRecoveryRpcResponse response_proto;
  EXPECT_TRUE(mediator_->MediateRequestPayload(
      /*epoch_pub_key=*/random_key, epoch_priv_key_, mediator_priv_key_,
      recovery_request, &response_proto));

  // `DecryptResponsePayload` fails if invalid epoch value was used for
  // `MediateRequestPayload`.
  HsmResponsePlainText response_plain_text;
  EXPECT_FALSE(recovery_->DecryptResponsePayload(
      generate_hsm_payload_response.encrypted_channel_priv_key, epoch_response_,
      response_proto,
      /*obfuscated_username=*/"", &response_plain_text));
}

TEST_F(RecoveryCryptoTest, RecoverDestinationInvalidDealerPublicKey) {
  SecureBlob recovery_key, destination_share, channel_priv_key,
      ephemeral_pub_key;
  CryptoRecoveryRpcResponse response_proto;
  GenerateSecretsAndMediate(&recovery_key, &destination_share,
                            &channel_priv_key, &ephemeral_pub_key,
                            &response_proto);

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(recovery_->DecryptResponsePayload(
      channel_priv_key, epoch_response_, response_proto,
      /*obfuscated_username=*/"", &response_plain_text));

  SecureBlob random_key = GeneratePublicKey();

  SecureBlob mediated_recovery_key;
  EXPECT_TRUE(recovery_->RecoverDestination(
      /*dealer_pub_key=*/random_key, response_plain_text.key_auth_value,
      destination_share, ephemeral_pub_key, response_plain_text.mediated_point,
      /*obfuscated_username=*/"", &mediated_recovery_key));

  // `mediated_recovery_key` is different from `recovery_key` when
  // `dealer_pub_key` is set to a wrong value.
  EXPECT_NE(recovery_key, mediated_recovery_key);
}

TEST_F(RecoveryCryptoTest, RecoverDestinationInvalidDestinationShare) {
  SecureBlob recovery_key, destination_share, channel_priv_key,
      ephemeral_pub_key, response_cbor;
  CryptoRecoveryRpcResponse response_proto;
  GenerateSecretsAndMediate(&recovery_key, &destination_share,
                            &channel_priv_key, &ephemeral_pub_key,
                            &response_proto);

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(recovery_->DecryptResponsePayload(
      channel_priv_key, epoch_response_, response_proto,
      /*obfuscated_username=*/"", &response_plain_text));

  SecureBlob random_scalar = GenerateScalar();

  SecureBlob mediated_recovery_key;
  EXPECT_TRUE(recovery_->RecoverDestination(
      response_plain_text.dealer_pub_key, response_plain_text.key_auth_value,
      /*destination_share=*/random_scalar, ephemeral_pub_key,
      response_plain_text.mediated_point, /*obfuscated_username=*/"",
      &mediated_recovery_key));

  // `mediated_recovery_key` is different from `recovery_key` when
  // `destination_share` is set to a wrong value.
  EXPECT_NE(recovery_key, mediated_recovery_key);
}

TEST_F(RecoveryCryptoTest, RecoverDestinationInvalidEphemeralKey) {
  SecureBlob recovery_key, destination_share, channel_priv_key,
      ephemeral_pub_key, response_cbor;
  CryptoRecoveryRpcResponse response_proto;
  GenerateSecretsAndMediate(&recovery_key, &destination_share,
                            &channel_priv_key, &ephemeral_pub_key,
                            &response_proto);

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(recovery_->DecryptResponsePayload(
      channel_priv_key, epoch_response_, response_proto,
      /*obfuscated_username=*/"", &response_plain_text));

  SecureBlob random_key = GeneratePublicKey();

  SecureBlob mediated_recovery_key;
  EXPECT_TRUE(recovery_->RecoverDestination(
      response_plain_text.dealer_pub_key, response_plain_text.key_auth_value,
      destination_share,
      /*ephemeral_pub_key=*/random_key, response_plain_text.mediated_point,
      /*obfuscated_username=*/"obfuscated_username", &mediated_recovery_key));

  // `mediated_recovery_key` is different from `recovery_key` when
  // `ephemeral_pub_key` is set to a wrong value.
  EXPECT_NE(recovery_key, mediated_recovery_key);
}

TEST_F(RecoveryCryptoTest, RecoverDestinationInvalidMediatedPointValue) {
  SecureBlob recovery_key, destination_share, channel_priv_key,
      ephemeral_pub_key, response_cbor;
  CryptoRecoveryRpcResponse response_proto;
  GenerateSecretsAndMediate(&recovery_key, &destination_share,
                            &channel_priv_key, &ephemeral_pub_key,
                            &response_proto);

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(recovery_->DecryptResponsePayload(
      channel_priv_key, epoch_response_, response_proto,
      /*obfuscated_username=*/"", &response_plain_text));

  SecureBlob random_key = GeneratePublicKey();

  SecureBlob mediated_recovery_key;
  EXPECT_TRUE(recovery_->RecoverDestination(
      response_plain_text.dealer_pub_key, response_plain_text.key_auth_value,
      destination_share, ephemeral_pub_key,
      /*mediated_point=*/random_key, /*obfuscated_username=*/"",
      &mediated_recovery_key));

  // `mediated_recovery_key` is different from `recovery_key` when
  // `mediated_point` is set to a wrong point.
  EXPECT_NE(recovery_key, mediated_recovery_key);
}

TEST_F(RecoveryCryptoTest, RecoverDestinationInvalidMediatedPoint) {
  SecureBlob recovery_key, destination_share, channel_priv_key,
      ephemeral_pub_key, response_cbor;
  CryptoRecoveryRpcResponse response_proto;
  GenerateSecretsAndMediate(&recovery_key, &destination_share,
                            &channel_priv_key, &ephemeral_pub_key,
                            &response_proto);

  HsmResponsePlainText response_plain_text;
  EXPECT_TRUE(recovery_->DecryptResponsePayload(
      channel_priv_key, epoch_response_, response_proto,
      /*obfuscated_username=*/"", &response_plain_text));

  // `RecoverDestination` fails when `mediated_point` is not a point.
  SecureBlob mediated_recovery_key;
  EXPECT_FALSE(recovery_->RecoverDestination(
      response_plain_text.dealer_pub_key, response_plain_text.key_auth_value,
      destination_share, ephemeral_pub_key,
      /*mediated_point=*/SecureBlob("not a point"), /*obfuscated_username=*/"",
      &mediated_recovery_key));
}

}  // namespace cryptorecovery
}  // namespace cryptohome
