// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/device_policy_service.h"

#include <secmodt.h>
#include <stdint.h>

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/containers/fixed_flat_map.h>
#include <base/containers/flat_map.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/types/expected.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/switches/chrome_switches.h>
#include <crypto/rsa_private_key.h>
#include <crypto/scoped_nss_types.h>
#include <install_attributes/libinstallattributes.h>
#include <libcrossystem/crossystem.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "crypto/signature_verifier.h"
#include "login_manager/blob_util.h"
#include "login_manager/dbus_util.h"
#include "login_manager/feature_flags_util.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_service_util.h"
#include "login_manager/policy_store.h"
#include "login_manager/system_utils.h"

namespace em = enterprise_management;

namespace login_manager {
using crypto::RSAPrivateKey;
using google::protobuf::RepeatedPtrField;

namespace {

constexpr auto attrs_to_ownership =
    base::MakeFixedFlatMap<std::string_view, LoginMetrics::OwnershipState>(
        {{"", LoginMetrics::OwnershipState::kConsumer},
         {InstallAttributesReader::kDeviceModeConsumerKiosk,
          LoginMetrics::OwnershipState::kConsumerKiosk},
         {InstallAttributesReader::kDeviceModeEnterprise,
          LoginMetrics::OwnershipState::kEnterprise},
         {InstallAttributesReader::kDeviceModeLegacyRetail,
          LoginMetrics::OwnershipState::kLegacyRetail}});

// Returns true if |policy| was not pushed by an enterprise.
bool IsConsumerPolicy(const em::PolicyFetchResponse& policy) {
  em::PolicyData poldata;
  if (!policy.has_policy_data() ||
      !poldata.ParseFromString(policy.policy_data())) {
    return false;
  }

  // Look at management_mode first. Refer to PolicyData::management_mode docs
  // for details.
  if (poldata.has_management_mode()) {
    return poldata.management_mode() == em::PolicyData::LOCAL_OWNER;
  }
  return !poldata.has_request_token() && poldata.has_username();
}

void HandleVpdUpdateCompletion(bool ignore_error,
                               PolicyService::Completion completion,
                               bool success) {
  if (completion.is_null()) {
    return;
  }

  if (success || ignore_error) {
    std::move(completion).Run(brillo::ErrorPtr());
    return;
  }

  LOG(ERROR) << "Failed to update VPD";
  std::move(completion)
      .Run(CreateError(dbus_error::kVpdUpdateFailed, "Failed to update VPD"));
}

}  // namespace

// static
const char DevicePolicyService::kPolicyDir[] = "/var/lib/devicesettings";
// static
const char DevicePolicyService::kDevicePolicyType[] = "google/chromeos/device";
// static
const char DevicePolicyService::kExtensionPolicyType[] =
    "google/chrome/extension";
// static
const char DevicePolicyService::kRemoteCommandPolicyType[] =
    "google/chromeos/remotecommand";

DevicePolicyService::~DevicePolicyService() = default;

// static
std::unique_ptr<DevicePolicyService> DevicePolicyService::Create(
    PolicyKey* owner_key,
    LoginMetrics* metrics,
    NssUtil* nss,
    SystemUtils* system_utils,
    crossystem::Crossystem* crossystem,
    VpdProcess* vpd_process,
    InstallAttributesReader* install_attributes_reader) {
  return base::WrapUnique(new DevicePolicyService(
      base::FilePath(kPolicyDir), owner_key, metrics, nss, system_utils,
      crossystem, vpd_process, install_attributes_reader));
}

bool DevicePolicyService::UserIsOwner(const std::string& current_user) {
  return GivenUserIsOwner(GetChromeStore()->Get(), current_user);
}

DevicePolicyService::DevicePolicyService(
    const base::FilePath& policy_dir,
    PolicyKey* policy_key,
    LoginMetrics* metrics,
    NssUtil* nss,
    SystemUtils* system_utils,
    crossystem::Crossystem* crossystem,
    VpdProcess* vpd_process,
    InstallAttributesReader* install_attributes_reader)
    : PolicyService(system_utils, policy_dir, policy_key, metrics, true),
      nss_(nss),
      system_utils_(system_utils),
      crossystem_(crossystem),
      vpd_process_(vpd_process),
      install_attributes_reader_(install_attributes_reader) {}

bool DevicePolicyService::Initialize() {
  bool key_success = key()->PopulateFromDiskIfPossible();
  if (!key_success) {
    LOG(ERROR) << "Failed to load device policy key from disk.";
  }

  bool policy_success = GetChromeStore()->EnsureLoadedOrCreated();
  if (!policy_success) {
    LOG(WARNING) << "Failed to load device policy data, continuing anyway.";
  }

  if (!key_success && policy_success &&
      GetChromeStore()->Get().has_new_public_key()) {
    LOG(WARNING) << "Recovering missing owner key from policy blob!";
    key_success = key()->PopulateFromBuffer(
        StringToBlob(GetChromeStore()->Get().new_public_key()));
    if (key_success) {
      PersistKey();
    }
  }

  if (install_attributes_reader_->IsLocked()) {
    ReportDevicePolicyFileMetrics(key_success, key()->IsPopulated(),
                                  policy_success,
                                  GetChromeStore()->Get().has_policy_data());
  }
  return key_success;
}

void DevicePolicyService::Store(const PolicyNamespace& ns,
                                const std::vector<uint8_t>& policy_blob,
                                int key_flags,
                                Completion completion) {
  if (ns == MakeChromePolicyNamespace()) {
    // Flush the settings cache, the next read will decode the new settings.
    // This has to be done before Store operation is started because Store()
    // persists the policy and triggers notification to SessionManagerImpl.
    // The later gets the new settings to pass to DeviceLocalAccount and at
    // that point the old settings_ have to be reset.
    //
    // The operations leading to notification to SessionManagerImpl are
    // synchronous, so when PolicyService::Store finishes, the new settings_
    // are already populated. Which makes it safe against possible requests
    // to GetSettings() that could come from other places.
    // TODO(b/274098828): Come up with a better way to handle the settings_
    // object so that its state change is cleaner.
    settings_.reset();
  }
  PolicyService::Store(ns, policy_blob, key_flags, std::move(completion));
}

std::vector<std::string> DevicePolicyService::GetFeatureFlags() {
  using Status = LoginMetrics::SwitchToFeatureFlagMappingStatus;
  auto status = Status::SWITCHES_ABSENT;
  std::vector<std::string> feature_flags;
  const em::ChromeDeviceSettingsProto& settings = GetSettings();

  if (settings.feature_flags().feature_flags_size() > 0) {
    for (const auto& feature_flag : settings.feature_flags().feature_flags()) {
      feature_flags.push_back(feature_flag);
    }
  } else {
    // Previous versions of this code allowed raw switches to be specified in
    // device settings, stored in the now deprecated |switches| proto message
    // field. In order to keep existing device settings data files working, map
    // these switches back to feature flags.
    // TODO(crbug/1104193): Remove compatibility code when no longer needed.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (settings.feature_flags().switches_size() > 0) {
      status = Status::SWITCHES_VALID;
      for (const auto& switch_string : settings.feature_flags().switches()) {
        if (!MapSwitchToFeatureFlags(switch_string, &feature_flags)) {
          LOG(WARNING) << "Invalid feature flag switch: " << switch_string;
          status = Status::SWITCHES_INVALID;
        }
      }
    }
#pragma GCC diagnostic pop
  }

  metrics_->SendSwitchToFeatureFlagMappingStatus(status);

  return feature_flags;
}

std::vector<std::string> DevicePolicyService::GetExtraCommandLineArguments() {
  if (GetSettings().has_devicehardwarevideodecodingenabled() &&
      !GetSettings().devicehardwarevideodecodingenabled().value()) {
    return {::chromeos::switches::kDisableAcceleratedVideoDecode};
  }
  return {};
}

const em::ChromeDeviceSettingsProto& DevicePolicyService::GetSettings() {
  if (!settings_.get()) {
    settings_.reset(new em::ChromeDeviceSettingsProto());

    em::PolicyData policy_data;
    if (!policy_data.ParseFromString(GetChromeStore()->Get().policy_data()) ||
        !settings_->ParseFromString(policy_data.policy_value())) {
      LOG(ERROR) << "Failed to parse device settings, using empty defaults.";
    }
  }

  return *settings_;
}

// static
bool DevicePolicyService::PolicyAllowsNewUsers(
    const em::PolicyFetchResponse& policy) {
  em::PolicyData poldata;
  if (!policy.has_policy_data() ||
      !poldata.ParseFromString(policy.policy_data())) {
    return false;
  }
  em::ChromeDeviceSettingsProto polval;
  if (!poldata.has_policy_type() ||
      poldata.policy_type() != DevicePolicyService::kDevicePolicyType ||
      !poldata.has_policy_value() ||
      !polval.ParseFromString(poldata.policy_value())) {
    return false;
  }
  // TODO(crbug.com/1103816) - remove whitelist support when no longer supported
  // by DMServer.
  bool has_whitelist_only =
      polval.has_user_whitelist() && !polval.has_user_allowlist();
  bool has_allowlist = has_whitelist_only || polval.has_user_allowlist();
  // Use the allowlist proto by default, and only look at the whitelist proto
  // iff there are no values set for the allowlist proto.
  bool non_empty_allowlist =
      has_whitelist_only ? (polval.has_user_whitelist() &&
                            polval.user_whitelist().user_whitelist_size() > 0)
                         : (polval.has_user_allowlist() &&
                            polval.user_allowlist().user_allowlist_size() > 0);
  // Explicitly states that new users are allowed.
  bool explicitly_allowed = (polval.has_allow_new_users() &&
                             polval.allow_new_users().allow_new_users());
  // Doesn't state that new users are allowed, but also doesn't have a
  // non-empty whitelist or allowlist.
  bool not_disallowed = !polval.has_allow_new_users() && !non_empty_allowlist;
  // States that new users are not allowed, but doesn't specify a whitelist.
  // So, we fail open. Such policies are the result of a long-fixed bug, but
  // we're not certain all users ever got migrated.
  bool failed_open = polval.has_allow_new_users() &&
                     !polval.allow_new_users().allow_new_users() &&
                     !has_allowlist;

  return explicitly_allowed || not_disallowed || failed_open;
}

// static
bool DevicePolicyService::GivenUserIsOwner(
    const enterprise_management::PolicyFetchResponse& policy,
    const std::string& current_user) {
  em::PolicyData poldata;
  if (!policy.has_policy_data() ||
      !poldata.ParseFromString(policy.policy_data())) {
    return false;
  }

  if (!IsConsumerPolicy(policy)) {
    return false;
  }

  return (poldata.has_username() && poldata.username() == current_user);
}

void DevicePolicyService::PersistPolicy(const PolicyNamespace& ns,
                                        Completion completion) {
  // Run base method for everything other than Chrome device policy.
  if (ns != MakeChromePolicyNamespace()) {
    PolicyService::PersistPolicy(ns, std::move(completion));
    return;
  }

  if (!GetOrCreateStore(ns)->Persist()) {
    OnPolicyPersisted(std::move(completion), dbus_error::kSigEncodeFail);
    return;
  }

  if (!MayUpdateSystemSettings()) {
    OnPolicyPersisted(std::move(completion), dbus_error::kNone);
    return;
  }

  auto split_callback = base::SplitOnceCallback(std::move(completion));
  if (UpdateSystemSettings(std::move(split_callback.first))) {
    // |vpd_process_| will run |completion| when it's done, so pass a null
    // completion to OnPolicyPersisted().
    OnPolicyPersisted(Completion(), dbus_error::kNone);
  } else {
    OnPolicyPersisted(std::move(split_callback.second),
                      dbus_error::kVpdUpdateFailed);
  }
}

bool DevicePolicyService::MayUpdateSystemSettings() {
  // Check if device ownership is established.
  if (!key()->IsPopulated()) {
    return false;
  }

  // Check whether device is running on ChromeOS firmware.
  std::optional<std::string> buffer = crossystem_->VbGetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareType);
  return buffer && buffer != crossystem::Crossystem::kMainfwTypeNonchrome;
}

bool DevicePolicyService::UpdateSystemSettings(Completion completion) {
  const int block_devmode_setting =
      GetSettings().system_settings().block_devmode();
  std::optional<int> block_devmode_value = crossystem_->VbGetSystemPropertyInt(
      crossystem::Crossystem::kBlockDevmode);
  if (!block_devmode_value) {
    LOG(ERROR) << "Failed to read block_devmode flag!";
  }

  // Set crossystem block_devmode flag.
  if (block_devmode_value != block_devmode_setting) {
    if (!crossystem_->VbSetSystemPropertyInt(
            crossystem::Crossystem::kBlockDevmode, block_devmode_setting)) {
      LOG(ERROR) << "Failed to write block_devmode flag!";
    } else {
      block_devmode_value.emplace(block_devmode_setting);
    }
  }

  // Clear nvram_cleared if block_devmode has the correct state now. (This is
  // OK as long as block_devmode is the only consumer of nvram_cleared. Once
  // other use cases crop up, clearing has to be done in cooperation.)
  if (block_devmode_value == block_devmode_setting) {
    std::optional<int> nvram_cleared_value =
        crossystem_->VbGetSystemPropertyInt(
            crossystem::Crossystem::kNvramCleared);
    if (!nvram_cleared_value) {
      LOG(ERROR) << "Failed to read nvram_cleared flag!";
    }
    if (nvram_cleared_value != 0) {
      if (!crossystem_->VbSetSystemPropertyInt(
              crossystem::Crossystem::kNvramCleared, 0)) {
        LOG(ERROR) << "Failed to clear nvram_cleared flag!";
      }
    }
  }

  // Used to keep the update key-value pairs for the VPD updater script.
  std::vector<std::pair<std::string, std::string>> updates;
  updates.push_back(std::make_pair(crossystem::Crossystem::kBlockDevmode,
                                   std::to_string(block_devmode_setting)));

  // Check if device is enrolled. The flag for enrolled device is written to VPD
  // but will never get deleted. Existence of the flag is one of the triggers
  // for FRE check during OOBE.
  if (!install_attributes_reader_->IsLocked()) {
    // Probably the first sign in, install attributes file is not created yet.
    if (!completion.is_null()) {
      std::move(completion).Run(brillo::ErrorPtr());
    }

    return true;
  }

  // If the install attributes are finalized (OOBE completed), try to delete the
  // Chromad migration skip OOBE flag. This insures that the file gets deleted
  // when it's no longer needed.
  //
  // TODO(b/263367348): Delete this `RemoveFile()` call, when all supported
  // devices are guaranteed to not have this file persisted.
  system_utils_->RemoveFile(
      base::FilePath(kChromadMigrationSkipOobePreservePath));

  bool is_enrolled =
      GetEnterpriseMode() == InstallAttributesReader::kDeviceModeEnterprise;

  // It's impossible for block_devmode to be true and the device to not be
  // enrolled. If we end up in this situation, log the error and don't update
  // anything in VPD. The exception is if the device is in devmode, but we are
  // fine with this limitation, since user can update VPD in devmode manually.
  if (block_devmode_setting && !is_enrolled) {
    LOG(ERROR) << "Can't store contradictory values in VPD";
    // Return true to be on the safe side here since not allowing to continue
    // would make the device unusable.
    if (!completion.is_null()) {
      std::move(completion).Run(brillo::ErrorPtr());
    }

    return true;
  }

  updates.push_back(std::make_pair(crossystem::Crossystem::kCheckEnrollment,
                                   std::to_string(is_enrolled)));

  // Note that VPD update errors will be ignored if the device is not enrolled.
  bool ignore_errors = !is_enrolled;
  return vpd_process_->RunInBackground(
      updates, base::BindOnce(&HandleVpdUpdateCompletion, ignore_errors,
                              std::move(completion)));
}

void DevicePolicyService::ClearBlockDevmode(Completion completion) {
  LOG(WARNING) << "Clear block_devmode requested";
  // The block_devmode system property needs to be set to 0 as well to unblock
  // dev mode. It is stored independently from VPD and firmware management
  // parameters.
  if (!crossystem_->VbSetSystemPropertyInt(
          crossystem::Crossystem::kBlockDevmode, 0)) {
    std::move(completion)
        .Run(CreateError(dbus_error::kSystemPropertyUpdateFailed,
                         "Failed to set block_devmode system property to 0."));
    return;
  }
  auto split_callback = base::SplitOnceCallback(std::move(completion));
  if (!vpd_process_->RunInBackground(
          {{crossystem::Crossystem::kBlockDevmode, "0"}},
          base::BindOnce(&HandleVpdUpdateCompletion, false,
                         std::move(split_callback.first)))) {
    std::move(split_callback.second)
        .Run(CreateError(dbus_error::kVpdUpdateFailed,
                         "Failed to run VPD update in the background."));
  }
}

bool DevicePolicyService::ValidateRemoteDeviceWipeCommand(
    const std::vector<uint8_t>& in_signed_command,
    em::PolicyFetchRequest::SignatureType signature_type) {
  // Parse the SignedData that was sent over the DBus call.
  em::SignedData signed_data;
  if (!signed_data.ParseFromArray(in_signed_command.data(),
                                  in_signed_command.size()) ||
      !signed_data.has_data() || !signed_data.has_signature()) {
    LOG(ERROR) << "SignedData parsing failed.";
    return false;
  }

  // TODO(isandrk, 1000627): Move into a common Verify() function that everyone
  // uses (signature verification & policy_type checking).

  // Verify the command signature.
  base::expected<crypto::SignatureVerifier::SignatureAlgorithm, std::string>
      mapped_signature_type = MapSignatureType(signature_type);
  if (!mapped_signature_type.has_value()) {
    LOG(ERROR) << "Invalid command signature type: " << signature_type;
    return false;
  }

  if (!key()->Verify(StringToBlob(signed_data.data()),
                     StringToBlob(signed_data.signature()),
                     mapped_signature_type.value())) {
    LOG(ERROR) << "Invalid command signature.";
    return false;
  }

  // Parse the PolicyData from the raw data.
  em::PolicyData policy_data;
  if (!policy_data.ParseFromString(signed_data.data())) {
    LOG(ERROR) << "PolicyData parsing failed.";
    return false;
  }

  // Verify that this PolicyData really contains the RemoteCommand.
  if (policy_data.policy_type() != kRemoteCommandPolicyType) {
    LOG(ERROR) << "Received PolicyData doesn't contain the RemoteCommand.";
    return false;
  }

  // Parse the RemoteCommand from the PolicyData.
  em::RemoteCommand remote_command;
  if (!remote_command.ParseFromString(policy_data.policy_value())) {
    LOG(ERROR) << "RemoteCommand parsing failed.";
    return false;
  }

  // Also verify command type and target device id here.
  if (remote_command.type() != em::RemoteCommand_Type_DEVICE_REMOTE_POWERWASH) {
    LOG(ERROR) << "Invalid remote command type.";
    return false;
  }
  if (remote_command.target_device_id() != GetDeviceId()) {
    LOG(ERROR) << "Invalid remote command target_device_id.";
    return false;
  }

  // Note: the code here doesn't protect against replay attacks, but that is not
  // an issue for remote powerwash since after execution the device ID will no
  // longer match. In case more commands are to be added in the future, replay
  // protection must be considered and added if deemed necessary.

  return true;
}

PolicyStore* DevicePolicyService::GetChromeStore() {
  return GetOrCreateStore(MakeChromePolicyNamespace());
}

std::string DevicePolicyService::GetDeviceId() {
  em::PolicyData policy_data;
  if (!policy_data.ParseFromString(GetChromeStore()->Get().policy_data()) ||
      !policy_data.has_device_id()) {
    LOG(ERROR) << "Failed to parse policy data, returning empty device id.";
    return std::string();
  }
  return policy_data.device_id();
}

const std::string& DevicePolicyService::GetEnterpriseMode() {
  return install_attributes_reader_->GetAttribute(
      InstallAttributesReader::kAttrMode);
}

void DevicePolicyService::ReportDevicePolicyFileMetrics(bool key_success,
                                                        bool key_populated,
                                                        bool policy_success,
                                                        bool policy_populated) {
  LoginMetrics::DevicePolicyFilesStatus status;
  if (!key_success) {  // Key load failed.
    status.owner_key_file_state = LoginMetrics::PolicyFileState::kMalformed;
  } else {
    if (key_populated) {
      status.owner_key_file_state = LoginMetrics::PolicyFileState::kGood;
    } else {
      status.owner_key_file_state = LoginMetrics::PolicyFileState::kNotPresent;
    }
  }

  if (!policy_success) {
    status.policy_file_state = LoginMetrics::PolicyFileState::kMalformed;
  } else {
    if (policy_populated) {
      status.policy_file_state = LoginMetrics::PolicyFileState::kGood;
    } else {
      status.policy_file_state = LoginMetrics::PolicyFileState::kNotPresent;
    }
  }

  std::string install_attributes_mode = GetEnterpriseMode();
  auto it = attrs_to_ownership.find(install_attributes_mode);
  if (it != attrs_to_ownership.end()) {
    status.ownership_state = it->second;
  } else {
    status.ownership_state = LoginMetrics::OwnershipState::kOther;
  }

  metrics_->SendDevicePolicyFilesMetrics(status);
}

bool DevicePolicyService::IsChromeStoreResilientForTesting() {
  return GetChromeStore()->resilient_for_testing();
}

}  // namespace login_manager
