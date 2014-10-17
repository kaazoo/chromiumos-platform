// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_factory_for_test.h"

#include <gmock/gmock.h>

#include "trunks/authorization_delegate.h"
#include "trunks/mock_tpm.h"
#include "trunks/mock_tpm_state.h"
#include "trunks/mock_tpm_utility.h"
#include "trunks/null_authorization_delegate.h"
#include "trunks/tpm_generated.h"
#include "trunks/tpm_state.h"
#include "trunks/tpm_utility.h"

using testing::NiceMock;

namespace trunks {

// Forwards all calls to a target instance.
class TpmStateForwarder : public TpmState {
 public:
  explicit TpmStateForwarder(TpmState* target) : target_(target) {}
  virtual ~TpmStateForwarder() {}

  TPM_RC Initialize() override {
    return target_->Initialize();
  }

  bool IsInLockout() override {
    return target_->IsInLockout();
  }

  bool IsPlatformHierarchyEnabled() override {
    return target_->IsPlatformHierarchyEnabled();
  }

  bool WasShutdownOrderly() override {
    return target_->WasShutdownOrderly();
  }

 private:
  TpmState* target_;
};

// Forwards all calls to a target instance.
class TpmUtilityForwarder : public TpmUtility {
 public:
  explicit TpmUtilityForwarder(TpmUtility* target) : target_(target) {}
  virtual ~TpmUtilityForwarder() {}

  TPM_RC Startup() override {
    return target_->Startup();
  }

  TPM_RC InitializeTpm() override {
    return target_->InitializeTpm();
  }

  TPM_RC StirRandom(const std::string& entropy_data) override {
    return target_->StirRandom(entropy_data);
  }

  TPM_RC GenerateRandom(int num_bytes, std::string* random_data) override {
    return target_->GenerateRandom(num_bytes, random_data);
  }

  TPM_RC ExtendPCR(int pcr_index, const std::string& extend_data) override {
    return target_->ExtendPCR(pcr_index, extend_data);
  }

  TPM_RC ReadPCR(int pcr_index, std::string* pcr_value) override {
    return target_->ReadPCR(pcr_index, pcr_value);
  }

 private:
  TpmUtility* target_;
};

// Forwards all calls to a target instance.
class AuthorizationDelegateForwarder : public AuthorizationDelegate {
 public:
  explicit AuthorizationDelegateForwarder(AuthorizationDelegate* target)
      : target_(target) {}
  virtual ~AuthorizationDelegateForwarder() {}

  bool GetCommandAuthorization(const std::string& command_hash,
                               std::string* authorization) override {
    return target_->GetCommandAuthorization(command_hash, authorization);
  }

  bool CheckResponseAuthorization(const std::string& response_hash,
                                  const std::string& authorization) override {
    return target_->CheckResponseAuthorization(response_hash, authorization);
  }

  bool EncryptCommandParameter(std::string* parameter) override {
    return target_->EncryptCommandParameter(parameter);
  }

  bool DecryptResponseParameter(std::string* parameter) override {
    return target_->DecryptResponseParameter(parameter);
  }

 private:
  AuthorizationDelegate* target_;
};

TrunksFactoryForTest::TrunksFactoryForTest()
    : default_tpm_(new NiceMock<MockTpm>()),
      tpm_(default_tpm_.get()),
      default_tpm_state_(new NiceMock<MockTpmState>()),
      tpm_state_(default_tpm_state_.get()),
      default_tpm_utility_(new NiceMock<MockTpmUtility>()),
      tpm_utility_(default_tpm_utility_.get()),
      default_authorization_delegate_(new NullAuthorizationDelegate()),
      password_authorization_delegate_(default_authorization_delegate_.get()) {
}

TrunksFactoryForTest::~TrunksFactoryForTest() {
}

Tpm* TrunksFactoryForTest::GetTpm() const {
  return tpm_;
}

scoped_ptr<TpmState> TrunksFactoryForTest::GetTpmState() const {
  return scoped_ptr<TpmState>(new TpmStateForwarder(tpm_state_));
}

scoped_ptr<TpmUtility> TrunksFactoryForTest::GetTpmUtility() const {
  return scoped_ptr<TpmUtility>(new TpmUtilityForwarder(tpm_utility_));
}

scoped_ptr<AuthorizationDelegate>
    TrunksFactoryForTest::GetPasswordAuthorization(
        const std::string& password) const {
  return scoped_ptr<AuthorizationDelegate>(
      new AuthorizationDelegateForwarder(password_authorization_delegate_));
}

}  // namespace trunks
