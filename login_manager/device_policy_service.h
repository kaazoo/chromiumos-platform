// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_DEVICE_POLICY_SERVICE_H_
#define LOGIN_MANAGER_DEVICE_POLICY_SERVICE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/gtest_prod_util.h>
#include <base/memory/ref_counted.h>
#include <crypto/scoped_nss_types.h>
#include <libcrossystem/crossystem.h>

#include "bindings/device_management_backend.pb.h"
#include "login_manager/nss_util.h"
#include "login_manager/policy_service.h"
#include "login_manager/vpd_process.h"

class InstallAttributesReader;

namespace crypto {
class RSAPrivateKey;
}

namespace enterprise_management {
class ChromeDeviceSettingsProto;
class PolicyFetchResponse;
}  // namespace enterprise_management

namespace login_manager {
class LoginMetrics;
class SystemUtils;

// A policy service specifically for device policy, adding in a few helpers for
// generating a new key for the device owner, handling key loss mitigation,
// storing owner properties etc.
class DevicePolicyService : public PolicyService {
 public:
  // Legacy flag file, used prior to M114 to indicate that some OOBE screens
  // should be skipped after the device was powerwashed - during the Chromad
  // migration to cloud management. See comment of the ".cc" file for details
  // about deleting this variable in the future.
  static inline constexpr char kChromadMigrationSkipOobePreservePath[] =
      "/mnt/stateful_partition/unencrypted/preserve/"
      "chromad_migration_skip_oobe";

  ~DevicePolicyService() override;

  // Instantiates a regular (non-testing) device policy service instance.
  static std::unique_ptr<DevicePolicyService> Create(
      PolicyKey* owner_key,
      LoginMetrics* metrics,
      NssUtil* nss,
      SystemUtils* system_utils,
      crossystem::Crossystem* crossystem,
      VpdProcess* vpd_process,
      InstallAttributesReader* install_attributes_reader);

  // Returns true if |current_user| is listed in device policy as the device
  // owner. Returns false if not, or if that cannot be determined.
  virtual bool UserIsOwner(const std::string& current_user);

  // Loads policy key and policy blob from disk. Returns true if at least the
  // key can be loaded (policy may not be present yet, which is OK).
  virtual bool Initialize();

  // Gets feature flags specified in device settings to pass to Chrome on
  // startup.
  virtual std::vector<std::string> GetFeatureFlags();

  // Gets extra command line arguments to pass to Chrome on startup.
  virtual std::vector<std::string> GetExtraCommandLineArguments();

  // Returns the currently active device settings.
  const enterprise_management::ChromeDeviceSettingsProto& GetSettings();

  // Returns whether system settings can be updated by checking that PolicyKey
  // is populated and the device is running on Chrome OS firmware.
  virtual bool MayUpdateSystemSettings();

  // Updates the system settings flags in NVRAM and in VPD. A failure in NVRAM
  // update is not considered a fatal error because new functionality relies on
  // VPD when checking the settings. The old code is using NVRAM however, which
  // means we have to update that memory too. Returns whether VPD process
  // started successfully and is running in a separate process. In this case,
  // |vpd_process_| is responsible for running |completion|; otherwise,
  // OnPolicyPersisted() is.
  virtual bool UpdateSystemSettings(Completion completion);

  // Sets the block_devmode flag in the VPD to 0 in the background. Also set
  // block_devmode=0 in system properties. If the update VPD process could be
  // started in the background |vpd_process_| is responsible for running
  // |completion|; otherwise, the completion is run with an error.
  virtual void ClearBlockDevmode(Completion completion);

  // Validates the remote device wipe command received from the server against
  // |signature_type| algorithm.
  // Does not allow em::PolicyFetchRequest::NONE signature type.
  virtual bool ValidateRemoteDeviceWipeCommand(
      const std::vector<uint8_t>& in_signed_command,
      enterprise_management::PolicyFetchRequest::SignatureType signature_type);

  // PolicyService:
  void Store(const PolicyNamespace& ns,
             const std::vector<uint8_t>& policy_blob,
             int key_flags,
             Completion completion) override;
  void PersistPolicy(const PolicyNamespace& ns, Completion completion) override;

  static const char kPolicyDir[];
  static const char kSerialRecoveryFlagFile[];

  // Format of this string is documented in device_management_backend.proto.
  static const char kDevicePolicyType[];
  static const char kExtensionPolicyType[];
  static const char kRemoteCommandPolicyType[];

 private:
  friend class DevicePolicyServiceTest;
  friend class MockDevicePolicyService;
  FRIEND_TEST_ALL_PREFIXES(DevicePolicyServiceTest, GivenUserIsOwner);
  FRIEND_TEST_ALL_PREFIXES(DevicePolicyServiceTest,
                           PersistPolicyMultipleNamespaces);

  // Takes ownership of |policy_store|.
  DevicePolicyService(const base::FilePath& policy_dir,
                      PolicyKey* owner_key,
                      LoginMetrics* metrics,
                      NssUtil* nss,
                      SystemUtils* system_utils,
                      crossystem::Crossystem* crossystem,
                      VpdProcess* vpd_process,
                      InstallAttributesReader* install_attributes_reader);
  DevicePolicyService(const DevicePolicyService&) = delete;
  DevicePolicyService& operator=(const DevicePolicyService&) = delete;

  // Returns true if |policy| allows arbitrary new users to sign in.
  // Only exposed for testing.
  static bool PolicyAllowsNewUsers(
      const enterprise_management::PolicyFetchResponse& policy);

  // Returns true if |current_user| is listed in |policy| as the device owner.
  // Returns false if not, or if that cannot be determined.
  static bool GivenUserIsOwner(
      const enterprise_management::PolicyFetchResponse& policy,
      const std::string& current_user);

  // Helper to return the policy store for the Chrome domain.
  PolicyStore* GetChromeStore();

  // Returns the device_id from PolicyData.
  std::string GetDeviceId();

  // Returns enterprise mode from |install_attributes_reader_|.
  const std::string& GetEnterpriseMode();

  // Process the input and send the metrics to UMA. |key_success| specifies
  // whether the key loading was successful (true also in case when there's yet
  // no key on disk), |key_populated| - if there's a key file on disk and it has
  // been successfully loaded. Similarly |policy_success| specifies whether the
  // policy loading was successful and |policy_populated| - if there's at least
  // one device policy file on disk that was successfully loaded.
  void ReportDevicePolicyFileMetrics(bool key_success,
                                     bool key_populated,
                                     bool policy_success,
                                     bool policy_populated);

  // Returns whether the store is resilient. To be used for testing only.
  bool IsChromeStoreResilientForTesting();

  NssUtil* nss_;
  SystemUtils* system_utils_;                           // Owned by the caller.
  crossystem::Crossystem* crossystem_;                  // Owned by the caller.
  VpdProcess* vpd_process_;                             // Owned by the caller.
  InstallAttributesReader* install_attributes_reader_;  // Owned by the caller.

  // Cached copy of the decoded device settings. Decoding happens on first
  // access, the cache is cleared whenever a new policy gets installed via
  // Store().
  std::unique_ptr<enterprise_management::ChromeDeviceSettingsProto> settings_;
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_DEVICE_POLICY_SERVICE_H_
