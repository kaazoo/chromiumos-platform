// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <utility>
#include <variant>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/flatbuffer_schemas/auth_block_state_test_utils.h"

using brillo::Blob;
using brillo::BlobFromString;
using brillo::SecureBlob;

namespace cryptohome {

TEST(AuthBlockStateBindingTest, EmptyState) {
  AuthBlockState state;
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, LibScryptCompatAuthBlockState) {
  AuthBlockState state = {
      .state = LibScryptCompatAuthBlockState{
          .wrapped_keyset = SecureBlob("wrapped_keyset"),
          .wrapped_chaps_key = SecureBlob("wrapped_chaps_key"),
          .wrapped_reset_seed = SecureBlob("wrapped_reset_seed"),
          .salt = SecureBlob("salt"),
      }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, LibScryptCompatAuthBlockStateEmpty) {
  AuthBlockState state = {.state = LibScryptCompatAuthBlockState{}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, LibScryptCompatAuthBlockStateNotEqual) {
  AuthBlockState state = {.state = LibScryptCompatAuthBlockState{}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  state.state = LibScryptCompatAuthBlockState{
      .wrapped_keyset = SecureBlob(""),
      .wrapped_chaps_key = SecureBlob(""),
      .wrapped_reset_seed = SecureBlob(""),
      .salt = SecureBlob(""),
  };
  EXPECT_NE(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmNotBoundToPcrAuthBlockState) {
  AuthBlockState state = {
      .state = TpmNotBoundToPcrAuthBlockState{
          .scrypt_derived = true,
          .salt = SecureBlob("salt"),
          .password_rounds = 1234,
          .tpm_key = SecureBlob("tpm_key"),
          .tpm_public_key_hash = SecureBlob("tpm_public_key_hash"),
      }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmNotBoundToPcrAuthBlockStateOptional) {
  AuthBlockState state1 = {.state = TpmNotBoundToPcrAuthBlockState{}};
  std::optional<SecureBlob> blob1 = state1.Serialize();
  ASSERT_TRUE(blob1.has_value());
  std::optional<AuthBlockState> state1_new =
      AuthBlockState::Deserialize(blob1.value());
  ASSERT_TRUE(state1_new.has_value());
  EXPECT_EQ(state1, state1_new);

  AuthBlockState state2 = {.state = TpmNotBoundToPcrAuthBlockState{
                               .password_rounds = 0,
                           }};
  std::optional<SecureBlob> blob2 = state2.Serialize();
  ASSERT_TRUE(blob2.has_value());
  std::optional<AuthBlockState> state2_new =
      AuthBlockState::Deserialize(blob2.value());
  ASSERT_TRUE(state2_new.has_value());
  EXPECT_EQ(state2, state2_new);

  AuthBlockState state3 = {.state = TpmNotBoundToPcrAuthBlockState{
                               .scrypt_derived = false,
                           }};
  std::optional<SecureBlob> blob3 = state3.Serialize();
  ASSERT_TRUE(blob3.has_value());
  std::optional<AuthBlockState> state3_new =
      AuthBlockState::Deserialize(blob3.value());
  ASSERT_TRUE(state3_new.has_value());
  EXPECT_EQ(state3, state3_new);

  AuthBlockState state4 = {.state = TpmNotBoundToPcrAuthBlockState{
                               .scrypt_derived = false,
                               .password_rounds = 0,
                           }};
  std::optional<SecureBlob> blob4 = state4.Serialize();
  ASSERT_TRUE(blob4.has_value());
  std::optional<AuthBlockState> state4_new =
      AuthBlockState::Deserialize(blob4.value());
  ASSERT_TRUE(state4_new.has_value());
  EXPECT_EQ(state4, state4_new);

  EXPECT_NE(state1, state2);
  EXPECT_NE(state1, state2_new);
  EXPECT_NE(state1_new, state2);
  EXPECT_NE(state1_new, state2_new);

  EXPECT_NE(state3, state4);
  EXPECT_NE(state3, state4_new);
  EXPECT_NE(state3_new, state4);
  EXPECT_NE(state3_new, state4_new);

  EXPECT_NE(state1, state3);
  EXPECT_NE(state2, state4);
  EXPECT_NE(state1, state3_new);
  EXPECT_NE(state2, state4_new);
  EXPECT_NE(state1_new, state3);
  EXPECT_NE(state2_new, state4);
  EXPECT_NE(state1_new, state3_new);
  EXPECT_NE(state2_new, state4_new);
}

TEST(AuthBlockStateBindingTest, TpmNotBoundToPcrAuthBlockStateEmpty) {
  AuthBlockState state = {.state = TpmNotBoundToPcrAuthBlockState{
                              .salt = SecureBlob(""),
                              .tpm_key = SecureBlob(""),
                              .tpm_public_key_hash = SecureBlob(""),
                          }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, DoubleWrappedCompatAuthBlockState) {
  AuthBlockState state = {
      .state = DoubleWrappedCompatAuthBlockState{
          .scrypt_state =
              LibScryptCompatAuthBlockState{
                  .wrapped_keyset = SecureBlob("wrapped_keyset"),
                  .wrapped_chaps_key = SecureBlob("wrapped_chaps_key"),
                  .wrapped_reset_seed = SecureBlob("wrapped_reset_seed"),
                  .salt = SecureBlob("salt"),
              },
          .tpm_state = TpmNotBoundToPcrAuthBlockState{
              .scrypt_derived = true,
              .salt = SecureBlob("salt"),
              .password_rounds = 1234,
              .tpm_key = SecureBlob("tpm_key"),
              .tpm_public_key_hash = SecureBlob("tpm_public_key_hash"),
          }}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateTpm12) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .scrypt_state =
              LibScryptCompatAuthBlockState{
                  .wrapped_keyset = SecureBlob("wrapped_keyset"),
                  .wrapped_chaps_key = SecureBlob("wrapped_chaps_key"),
                  .wrapped_reset_seed = SecureBlob("wrapped_reset_seed"),
                  .salt = SecureBlob("salt"),
              },
          .keyset_challenge_info = structure::SignatureChallengeInfo{
              .public_key_spki_der = BlobFromString("public_key_spki_der"),
              .sealed_secret =
                  structure::Tpm12CertifiedMigratableKeyData{
                      .public_key_spki_der =
                          BlobFromString("public_key_spki_der"),
                      .srk_wrapped_cmk = BlobFromString("srk_wrapped_cmk"),
                      .cmk_pubkey = BlobFromString("cmk_pubkey"),
                      .cmk_wrapped_auth_data =
                          BlobFromString("cmk_wrapped_auth_data"),
                      .default_pcr_bound_secret =
                          BlobFromString("default_pcr_bound_secret"),
                      .extended_pcr_bound_secret =
                          BlobFromString("extended_pcr_bound_secret"),
                  },
              .salt = BlobFromString("salt"),
              .salt_signature_algorithm =
                  structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
          }}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateTpm2) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .scrypt_state =
              LibScryptCompatAuthBlockState{
                  .wrapped_keyset = SecureBlob("wrapped_keyset"),
                  .wrapped_chaps_key = SecureBlob("wrapped_chaps_key"),
                  .wrapped_reset_seed = SecureBlob("wrapped_reset_seed"),
                  .salt = SecureBlob("salt"),
              },
          .keyset_challenge_info = structure::SignatureChallengeInfo{
              .public_key_spki_der = BlobFromString("public_key_spki_der"),
              .sealed_secret =
                  structure::Tpm2PolicySignedData{
                      .public_key_spki_der =
                          BlobFromString("public_key_spki_der"),
                      .srk_wrapped_secret =
                          BlobFromString("srk_wrapped_secret"),
                      .scheme = 5566,
                      .hash_alg = 7788,
                      .default_pcr_policy_digest =
                          BlobFromString("default_pcr_policy_digest"),
                      .extended_pcr_policy_digest =
                          BlobFromString("extended_pcr_policy_digest"),
                  },
              .salt = BlobFromString("salt"),
              .salt_signature_algorithm =
                  structure::ChallengeSignatureAlgorithm::kRsassaPkcs1V15Sha256,
          }}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateEmpty) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .scrypt_state =
              LibScryptCompatAuthBlockState{
                  .wrapped_keyset = SecureBlob(""),
                  .wrapped_chaps_key = SecureBlob(""),
                  .wrapped_reset_seed = SecureBlob(""),
                  .salt = SecureBlob(""),
              },
          .keyset_challenge_info = structure::SignatureChallengeInfo{
              .public_key_spki_der = BlobFromString(""),
              .sealed_secret =
                  structure::Tpm2PolicySignedData{
                      .public_key_spki_der = BlobFromString(""),
                      .srk_wrapped_secret = BlobFromString(""),
                      .default_pcr_policy_digest = BlobFromString(""),
                      .extended_pcr_policy_digest = BlobFromString(""),
                  },
              .salt = BlobFromString(""),
          }}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateNoInfo) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .scrypt_state =
              LibScryptCompatAuthBlockState{
                  .wrapped_keyset = SecureBlob("wrapped_keyset"),
                  .wrapped_chaps_key = SecureBlob("wrapped_chaps_key"),
                  .wrapped_reset_seed = SecureBlob("wrapped_reset_seed"),
                  .salt = SecureBlob("salt"),
              },
      }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, ChallengeCredentialAuthBlockStateDefault) {
  AuthBlockState state = {
      .state = ChallengeCredentialAuthBlockState{
          .keyset_challenge_info = structure::SignatureChallengeInfo{
              .sealed_secret = structure::Tpm2PolicySignedData{},
          }}};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
  state.state = ChallengeCredentialAuthBlockState{
      .keyset_challenge_info = structure::SignatureChallengeInfo{
          .public_key_spki_der = BlobFromString(""),
          .sealed_secret =
              structure::Tpm2PolicySignedData{
                  .public_key_spki_der = BlobFromString(""),
                  .srk_wrapped_secret = BlobFromString(""),
                  .default_pcr_policy_digest = BlobFromString(""),
                  .extended_pcr_policy_digest = BlobFromString(""),
              },
          .salt = BlobFromString(""),
      }};
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmBoundToPcrAuthBlockState) {
  AuthBlockState state = {
      .state = TpmBoundToPcrAuthBlockState{
          .scrypt_derived = false,
          .salt = SecureBlob("salt"),
          .tpm_key = SecureBlob("tpm_key"),
          .extended_tpm_key = SecureBlob("extended_tpm_key"),
          .tpm_public_key_hash = SecureBlob("tpm_public_key_hash"),
      }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, PinWeaverAuthBlockState) {
  AuthBlockState state = {.state = PinWeaverAuthBlockState{
                              .le_label = 0x1337,
                              .salt = SecureBlob("salt"),
                              .chaps_iv = SecureBlob("chaps_iv"),
                              .fek_iv = SecureBlob("fek_iv"),
                          }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, CryptohomeRecoveryAuthBlockState) {
  AuthBlockState state = {.state = CryptohomeRecoveryAuthBlockState{
                              .hsm_payload = SecureBlob("hsm_payload"),
                              .salt = SecureBlob("salt"),
                              .plaintext_destination_share =
                                  SecureBlob("plaintext_destination_share"),
                              .channel_pub_key = std::nullopt,
                              .channel_priv_key = std::nullopt,
                          }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}

TEST(AuthBlockStateBindingTest, TpmEccAuthBlockState) {
  AuthBlockState state = {
      .state = TpmEccAuthBlockState{
          .salt = SecureBlob("salt"),
          .vkk_iv = SecureBlob("vkk_iv"),
          .auth_value_rounds = 5,
          .sealed_hvkkm = SecureBlob("sealed_hvkkm"),
          .extended_sealed_hvkkm = SecureBlob("extended_sealed_hvkkm"),
          .tpm_public_key_hash = std::nullopt,
          .wrapped_reset_seed = SecureBlob("wrapped_reset_seed"),
      }};
  std::optional<SecureBlob> blob = state.Serialize();
  ASSERT_TRUE(blob.has_value());
  std::optional<AuthBlockState> state2 =
      AuthBlockState::Deserialize(blob.value());
  ASSERT_TRUE(state2.has_value());
  EXPECT_EQ(state, state2);
}
}  // namespace cryptohome
