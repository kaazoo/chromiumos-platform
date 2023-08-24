// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_IMPL_H_
#define LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_IMPL_H_

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "libhwsec/backend/pinweaver.h"
#include "libhwsec/backend/pinweaver_manager/sign_in_hash_tree.h"
#include "libhwsec/status.h"

using CredentialTreeResult = hwsec::PinWeaver::CredentialTreeResult;
namespace hwsec {

// Class containing all logic pertaining to management of Low Entropy(LE)
// credentials. The stated aim of this class should be the following:
// - Provide an interface to Set and Remove credentials in the underlying
// storage.
// - Provide an interface to verify a credential.
//
// This class contains a SignInHashTree object, which is used to store and
// maintain the credentials on disk.
//
// It also contains a pointer to a TPM object which will be able to invoke the
// necessary commands on the TPM side, for verification.
class LECredentialManagerImpl : public LECredentialManager {
 public:
  LECredentialManagerImpl(PinWeaver& pinweaver,
                          const base::FilePath& le_basedir)
      : pinweaver_(pinweaver), basedir_(le_basedir) {}

  virtual ~LECredentialManagerImpl() {}

  StatusOr<uint64_t> InsertCredential(
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob& le_secret,
      const brillo::SecureBlob& he_secret,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay) override;

  StatusOr<CheckCredentialReply> CheckCredential(
      uint64_t label, const brillo::SecureBlob& le_secret) override;

  Status ResetCredential(uint64_t label,
                         const brillo::SecureBlob& reset_secret,
                         ResetType reset_type) override;

  Status RemoveCredential(uint64_t label) override;

  StatusOr<uint32_t> GetWrongAuthAttempts(uint64_t label) override;

  StatusOr<uint32_t> GetDelayInSeconds(uint64_t label) override;

  StatusOr<std::optional<uint32_t>> GetExpirationInSeconds(
      uint64_t label) override;

  StatusOr<DelaySchedule> GetDelaySchedule(uint64_t label) override;

  StatusOr<uint64_t> InsertRateLimiter(
      uint8_t auth_channel,
      const std::vector<hwsec::OperationPolicySetting>& policies,
      const brillo::SecureBlob& reset_secret,
      const DelaySchedule& delay_sched,
      std::optional<uint32_t> expiration_delay) override;

  StatusOr<StartBiometricsAuthReply> StartBiometricsAuth(
      uint8_t auth_channel,
      uint64_t label,
      const brillo::Blob& client_nonce) override;

 private:
  // Performs checks to ensure the SignInHashTree is at good state. This
  // includes insuring that PinWeaverManager is initialized, not locked out,
  // and hash tree is valid.
  // All public PW operation functions should first call StateIsReady().
  Status StateIsReady();

  // Last resort flag which prevents any further Low Entropy operations from
  // occurring, till the next time the class is instantiated.
  // This is used in a situation where an operation succeeds on the TPM,
  // but its on-disk counterpart fails. In this case, the mitigation strategy
  // is as follows:
  // - Prevent any further LE operations, to prevent disk and TPM from
  // going further out of state, till next reboot.
  // - Hope that on reboot, the problems causing disk failure don't recur,
  // and the TPM replay log will enable the disk state to get in sync with
  // the TPM again.
  //
  // We will collect UMA stats from the field and refine this strategy
  // as required.
  bool is_initialized_ = false;
  bool is_locked_ = false;
  // In-memory copy of LEBackend's root hash value.
  brillo::Blob root_hash_;
  // Reference of an implementation of the pinweaver operations.
  PinWeaver& pinweaver_;
  // Directory where all LE Credential related data is stored.
  base::FilePath basedir_;
  std::unique_ptr<SignInHashTree> hash_tree_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_PINWEAVER_MANAGER_PINWEAVER_MANAGER_IMPL_H_
