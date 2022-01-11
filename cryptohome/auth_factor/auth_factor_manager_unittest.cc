// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "cryptohome/auth_blocks/auth_block_state.h"
#include "cryptohome/auth_factor/auth_factor.h"
#include "cryptohome/auth_factor/auth_factor_manager.h"
#include "cryptohome/auth_factor/auth_factor_metadata.h"
#include "cryptohome/auth_factor/auth_factor_type.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/mock_platform.h"

using brillo::SecureBlob;

namespace cryptohome {

namespace {

const char kObfuscatedUsername[] = "obfuscated1";
const char kSomeIdpLabel[] = "some-idp";

AuthBlockState CreatePasswordAuthBlockState() {
  TpmBoundToPcrAuthBlockState tpm_bound_to_pcr_auth_block_state = {
      .salt = SecureBlob("fake salt"),
      .tpm_key = SecureBlob("fake tpm key"),
      .extended_tpm_key = SecureBlob("fake extended tpm key"),
      .tpm_public_key_hash = SecureBlob("fake tpm public key hash"),
  };
  AuthBlockState auth_block_state = {.state =
                                         tpm_bound_to_pcr_auth_block_state};
  return auth_block_state;
}

std::unique_ptr<AuthFactor> CreatePasswordAuthFactor() {
  AuthFactorMetadata metadata = {.metadata = PasswordAuthFactorMetadata()};
  return std::make_unique<AuthFactor>(AuthFactorType::kPassword, kSomeIdpLabel,
                                      metadata, CreatePasswordAuthBlockState());
}

}  // namespace

class AuthFactorManagerTest : public ::testing::Test {
 protected:
  MockPlatform platform_;
  AuthFactorManager auth_factor_manager_{&platform_};
};

// Test the `SaveAuthFactor()` method correctly serializes the factor into a
// file.
TEST_F(AuthFactorManagerTest, Save) {
  std::unique_ptr<AuthFactor> auth_factor = CreatePasswordAuthFactor();

  // Persist the auth factor.
  EXPECT_TRUE(
      auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername, *auth_factor));
  EXPECT_TRUE(platform_.FileExists(
      AuthFactorPath(kObfuscatedUsername,
                     /*auth_factor_type_string=*/"password", kSomeIdpLabel)));

  // TODO(b/208348570): Test the factor can be loaded back.
}

// Test the `SaveAuthFactor()` method fails when the label is empty.
TEST_F(AuthFactorManagerTest, SaveBadEmptyLabel) {
  // Create an auth factor as a clone of a correct object, but with an empty
  // label.
  std::unique_ptr<AuthFactor> good_auth_factor = CreatePasswordAuthFactor();
  AuthFactor bad_auth_factor(good_auth_factor->type().value(),
                             /*label=*/std::string(),
                             good_auth_factor->metadata().value(),
                             good_auth_factor->auth_block_state().value());

  // Verify the manager refuses to save this auth factor.
  EXPECT_FALSE(auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername,
                                                   bad_auth_factor));
}

// Test the `SaveAuthFactor()` method fails when the label contains forbidden
// characters.
TEST_F(AuthFactorManagerTest, SaveBadMalformedLabel) {
  // Create an auth factor as a clone of a correct object, but with a malformed
  // label.
  std::unique_ptr<AuthFactor> good_auth_factor = CreatePasswordAuthFactor();
  AuthFactor bad_auth_factor(good_auth_factor->type().value(),
                             /*label=*/"foo.' bar'",
                             good_auth_factor->metadata().value(),
                             good_auth_factor->auth_block_state().value());

  // Verify the manager refuses to save this auth factor.
  EXPECT_FALSE(auth_factor_manager_.SaveAuthFactor(kObfuscatedUsername,
                                                   bad_auth_factor));
}

}  // namespace cryptohome
