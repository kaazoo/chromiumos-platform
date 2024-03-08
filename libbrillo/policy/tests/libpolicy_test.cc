// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "policy/libpolicy.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "brillo/secure_blob.h"
#include "gmock/gmock.h"
#include "install_attributes/mock_install_attributes_reader.h"
#include "policy/device_policy_impl.h"
#include "policy/tests/crypto_helpers.h"

namespace {

static const char kNonExistingFile[] = "file-does-not-exist";

// TODO(b/328406847): Split into individual test cases.
enterprise_management::ChromeDeviceSettingsProto
CreateFullySetPolicyDataValue() {
  enterprise_management::ChromeDeviceSettingsProto policy_data_value;

  // Device reporting settings.
  enterprise_management::DeviceReportingProto* mutable_device_reporting =
      policy_data_value.mutable_device_reporting();
  mutable_device_reporting->set_report_version_info(false);
  mutable_device_reporting->set_report_activity_times(false);
  mutable_device_reporting->set_report_boot_mode(false);
  mutable_device_reporting->set_report_cpu_info(false);
  mutable_device_reporting->set_report_graphics_status(false);
  mutable_device_reporting->set_report_memory_info(false);
  mutable_device_reporting->set_report_system_info(false);
  mutable_device_reporting->set_report_network_configuration(false);
  // Auto-update settings.
  enterprise_management::AutoUpdateSettingsProto* mutable_auto_update_settings =
      policy_data_value.mutable_auto_update_settings();
  mutable_auto_update_settings->set_update_disabled(false);
  mutable_auto_update_settings->set_target_version_prefix("42.0.");
  mutable_auto_update_settings->set_scatter_factor_in_seconds(17);
  mutable_auto_update_settings->add_allowed_connection_types(
      enterprise_management::AutoUpdateSettingsProto::CONNECTION_TYPE_ETHERNET);
  mutable_auto_update_settings->add_allowed_connection_types(
      enterprise_management::AutoUpdateSettingsProto::CONNECTION_TYPE_WIFI);
  mutable_auto_update_settings->set_http_downloads_enabled(false);
  mutable_auto_update_settings->set_p2p_enabled(false);
  mutable_auto_update_settings->set_rollback_to_target_version(
      enterprise_management::AutoUpdateSettingsProto::ROLLBACK_AND_POWERWASH);
  mutable_auto_update_settings->set_rollback_allowed_milestones(3);
  mutable_auto_update_settings->set_disallowed_time_intervals(R"(
      [
        {
          "start": {
            "day_of_week": "Thursday",
            "minutes": 30,
            "hours": 12
          },
          "end": {
            "day_of_week": "Saturday",
            "minutes": 15,
            "hours": 3
          }
        },
        {
          "start": {
            "day_of_week": "Monday",
            "minutes": 10,
            "hours": 20
          },
          "end": {
            "day_of_week": "Wednesday",
            "minutes": 20,
            "hours": 0
          }
        }
      ]
    )");
  mutable_auto_update_settings->set_target_version_selector("0,1626155736-");
  policy_data_value.mutable_device_minimum_version()->set_value(R"(
      {
        "requirements": [
          {
            "chromeos_version": "12215",
            "warning_period": 7,
            "aue_warning_period": 14
          },
          {
            "chromeos_version": "13315.60.12",
            "warning_period": 5,
            "aue_warning_period": 13
          },
          {
            "chromeos_version": "not-a-version"
          }
        ],
        "unmanaged_user_restricted": true
      }
    )");
  policy_data_value.mutable_allow_kiosk_app_control_chrome_version()
      ->set_allow_kiosk_app_control_chrome_version(false);
  // Device-local accounts.
  enterprise_management::DeviceLocalAccountsProto*
      mutable_device_local_accounts =
          policy_data_value.mutable_device_local_accounts();
  enterprise_management::DeviceLocalAccountInfoProto* account =
      mutable_device_local_accounts->add_account();
  account->set_account_id("abc");
  account->set_type(enterprise_management::DeviceLocalAccountInfoProto::
                        ACCOUNT_TYPE_PUBLIC_SESSION);
  account = mutable_device_local_accounts->add_account();
  account->set_account_id("def");
  account->set_type(enterprise_management::DeviceLocalAccountInfoProto::
                        ACCOUNT_TYPE_KIOSK_APP);
  account->mutable_kiosk_app()->set_app_id("my_kiosk_app");
  account = mutable_device_local_accounts->add_account();
  account->set_account_id("ghi");
  account->set_type(enterprise_management::DeviceLocalAccountInfoProto::
                        ACCOUNT_TYPE_KIOSK_APP);
  mutable_device_local_accounts->set_auto_login_id("def");
  mutable_device_local_accounts->set_auto_login_delay(0);
  // Usb settings.
  enterprise_management::UsbDeviceIdProto* usb_whitelist_id =
      policy_data_value.mutable_usb_detachable_whitelist()->add_id();
  usb_whitelist_id->set_vendor_id(465);
  usb_whitelist_id->set_product_id(57005);
  enterprise_management::UsbDeviceIdInclusiveProto* usb_allowlist_id =
      policy_data_value.mutable_usb_detachable_allowlist()->add_id();
  usb_allowlist_id->set_vendor_id(16700);
  usb_allowlist_id->set_product_id(8453);
  usb_allowlist_id =
      policy_data_value.mutable_usb_detachable_allowlist()->add_id();
  usb_allowlist_id->set_vendor_id(1027);
  usb_allowlist_id->set_product_id(24577);
  // Rest of the policies.
  policy_data_value.mutable_device_second_factor_authentication()->set_mode(
      enterprise_management::DeviceSecondFactorAuthenticationProto::U2F);
  policy_data_value.mutable_device_policy_refresh_rate()
      ->set_device_policy_refresh_rate(100);
  policy_data_value.mutable_guest_mode_enabled()->set_guest_mode_enabled(false);
  policy_data_value.mutable_camera_enabled()->set_camera_enabled(false);
  policy_data_value.mutable_show_user_names()->set_show_user_names(false);
  policy_data_value.mutable_data_roaming_enabled()->set_data_roaming_enabled(
      false);
  policy_data_value.mutable_allow_new_users()->set_allow_new_users(false);
  policy_data_value.mutable_metrics_enabled()->set_metrics_enabled(false);

  policy_data_value.mutable_data_roaming_enabled()->set_data_roaming_enabled(
      false);
  policy_data_value.mutable_release_channel()->set_release_channel(
      "stable-channel");
  policy_data_value.mutable_release_channel()->set_release_channel_delegated(
      true);
  policy_data_value.mutable_open_network_configuration()
      ->set_open_network_configuration("{}");
  policy_data_value.mutable_ephemeral_users_enabled()
      ->set_ephemeral_users_enabled(false);
  policy_data_value.mutable_auto_clean_up_settings()->set_clean_up_strategy(
      "remove-lru");
  policy_data_value.mutable_hardware_data_usage_enabled()
      ->set_hardware_data_usage_enabled(false);
  policy_data_value
      .mutable_device_flex_hw_data_for_product_improvement_enabled()
      ->set_enabled(false);
  policy_data_value.mutable_deviceextendedautoupdateenabled()->set_value(true);

  return policy_data_value;
}

// Generates a private and public key pair, signs |policy_data_value|,
// constructs PolicyFetchResponse proto.
// Persists the proto to |policy_path|, and the public key to
// |public_key_path|.
// Returns false if fails to persist, true otherwise.
bool SignAndPersist(
    const enterprise_management::ChromeDeviceSettingsProto& policy_data_value,
    const base::FilePath& policy_path,
    const base::FilePath& public_key_path) {
  enterprise_management::PolicyData policy_data;
  policy_data.set_username("");
  policy_data.set_policy_type("google/chromeos/device");
  policy_data_value.SerializeToString(policy_data.mutable_policy_value());
  std::string serialized_policy_data;
  policy_data.SerializeToString(&serialized_policy_data);

  // TODO(b/328427460): Replace with hardcoded keys to avoid expensive
  // regeneration.
  const policy::KeyPair key_pair = policy::GenerateRsaKeyPair();
  const brillo::Blob signature =
      policy::SignData(serialized_policy_data, *key_pair.private_key);
  enterprise_management::PolicyFetchResponse policy_fetch_response;
  policy_fetch_response.set_policy_data(serialized_policy_data);
  policy_fetch_response.set_policy_data_signature(
      brillo::BlobToString(signature));
  const std::string public_key = brillo::BlobToString(key_pair.public_key);
  policy_fetch_response.set_new_public_key(public_key);

  // Clients are expected to clean up in case of errors.
  return base::WriteFile(public_key_path, public_key) &&
         base::WriteFile(policy_path,
                         policy_fetch_response.SerializeAsString());
}

}  // namespace

namespace policy {

class LibpolicyTest : public testing::Test {
 public:
  void SetUp() override { ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir()); }

  // Creates the DevicePolicyImpl with given parameters for test.
  std::unique_ptr<DevicePolicyImpl> CreateDevicePolicyImpl(
      std::unique_ptr<InstallAttributesReader> install_attributes_reader,
      const base::FilePath& policy_path,
      const base::FilePath& keyfile_path,
      bool verify_files) {
    std::unique_ptr<DevicePolicyImpl> device_policy =
        std::make_unique<DevicePolicyImpl>();
    device_policy->set_install_attributes_for_testing(
        std::move(install_attributes_reader));
    device_policy->set_policy_path_for_testing(policy_path);
    device_policy->set_key_file_path_for_testing(keyfile_path);
    device_policy->set_verify_root_ownership_for_testing(verify_files);

    return device_policy;
  }

  const base::FilePath& GetTmpDirPath() { return tmp_dir_.GetPath(); }

 private:
  base::ScopedTempDir tmp_dir_;
};

// Test that a policy file can be verified and parsed correctly. The file
// contains all possible fields, so reading should succeed for all.
TEST_F(LibpolicyTest, DevicePolicyAllSetTest) {
  const enterprise_management::ChromeDeviceSettingsProto policy_data_value =
      CreateFullySetPolicyDataValue();
  const auto& policy_file = GetTmpDirPath().Append("policy");
  const auto& key_file = GetTmpDirPath().Append("key");
  ASSERT_TRUE(SignAndPersist(policy_data_value, policy_file, key_file));

  PolicyProvider provider;
  provider.SetDevicePolicyForTesting(CreateDevicePolicyImpl(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeEnterprise, true),
      policy_file, key_file, false));
  provider.Reload();
  // Ensure we successfully loaded the device policy file.
  ASSERT_TRUE(provider.device_policy_is_loaded());

  const DevicePolicy& policy = provider.GetDevicePolicy();

  // Check that we can read out all fields of the sample protobuf.
  auto refresh_rate = policy.GetPolicyRefreshRate();
  EXPECT_EQ(100, refresh_rate);

  bool bool_value = true;
  ASSERT_TRUE(policy.GetCameraEnabled(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetShowUserNames(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetDataRoamingEnabled(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetAllowNewUsers(&bool_value));
  EXPECT_FALSE(bool_value);

  EXPECT_THAT(policy.GetMetricsEnabled(), testing::Optional(false));

  bool_value = true;
  ASSERT_TRUE(policy.GetReportVersionInfo(&bool_value));
  EXPECT_FALSE(bool_value);

  std::optional<bool> optional_bool = policy.GetUnenrolledHwDataUsageEnabled();
  ASSERT_TRUE(optional_bool.has_value());
  EXPECT_FALSE(*optional_bool);

  optional_bool = policy.GetEnrolledHwDataUsageEnabled();
  ASSERT_TRUE(optional_bool.has_value());
  EXPECT_FALSE(*optional_bool);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportSystemInfo(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportCpuInfo(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportGraphicsStatus(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportMemoryInfo(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportNetworkConfig(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportActivityTimes(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetReportBootMode(&bool_value));
  EXPECT_FALSE(bool_value);

  DevicePolicy::EphemeralSettings ephemeral_settings;
  ASSERT_TRUE(policy.GetEphemeralSettings(&ephemeral_settings));
  EXPECT_FALSE(ephemeral_settings.global_ephemeral_users_enabled);

  std::string string_value;
  ASSERT_TRUE(policy.GetReleaseChannel(&string_value));
  EXPECT_EQ("stable-channel", string_value);

  bool_value = false;
  ASSERT_TRUE(policy.GetReleaseChannelDelegated(&bool_value));
  EXPECT_TRUE(bool_value);

  std::optional<bool> extended_auto_update_enabled =
      policy.GetDeviceExtendedAutoUpdateEnabled();
  ASSERT_TRUE(extended_auto_update_enabled.has_value());
  EXPECT_TRUE(*extended_auto_update_enabled);

  bool_value = true;
  ASSERT_TRUE(policy.GetUpdateDisabled(&bool_value));
  EXPECT_FALSE(bool_value);

  int64_t int64_value = -1LL;
  ASSERT_TRUE(policy.GetScatterFactorInSeconds(&int64_value));
  EXPECT_EQ(17LL, int64_value);

  ASSERT_TRUE(policy.GetTargetVersionPrefix(&string_value));
  EXPECT_EQ("42.0.", string_value);

  int int_value = -1;
  if (USE_ENTERPRISE_ROLLBACK_REVEN) {
    ASSERT_FALSE(policy.GetRollbackToTargetVersion(&int_value));
  } else {
    ASSERT_TRUE(policy.GetRollbackToTargetVersion(&int_value));
    EXPECT_EQ(
        enterprise_management::AutoUpdateSettingsProto::ROLLBACK_AND_POWERWASH,
        int_value);
  }

  int_value = -1;
  ASSERT_TRUE(policy.GetRollbackAllowedMilestones(&int_value));
  EXPECT_EQ(3, int_value);

  std::set<std::string> types;
  ASSERT_TRUE(policy.GetAllowedConnectionTypesForUpdate(&types));
  EXPECT_TRUE(types.end() != types.find("ethernet"));
  EXPECT_TRUE(types.end() != types.find("wifi"));
  EXPECT_EQ(2, types.size());

  ASSERT_TRUE(policy.GetOpenNetworkConfiguration(&string_value));
  EXPECT_EQ("{}", string_value);

  ASSERT_TRUE(policy.GetOwner(&string_value));
  EXPECT_EQ("", string_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetHttpDownloadsEnabled(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetAuP2PEnabled(&bool_value));
  EXPECT_FALSE(bool_value);

  bool_value = true;
  ASSERT_TRUE(policy.GetAllowKioskAppControlChromeVersion(&bool_value));
  EXPECT_FALSE(bool_value);

  // Note: policy data contains both the old usb_detachable_whitelist and the
  // new usb_detachable_allowlist.
  //
  // Test that only the allowlist is considered.
  std::vector<DevicePolicy::UsbDeviceId> list_device;
  ASSERT_TRUE(policy.GetUsbDetachableWhitelist(&list_device));
  ASSERT_EQ(2, list_device.size());
  // In the new usb_detachable_allowlist.
  EXPECT_EQ(0x413c, list_device[0].vendor_id);
  EXPECT_EQ(0x2105, list_device[0].product_id);
  EXPECT_EQ(0x0403, list_device[1].vendor_id);
  EXPECT_EQ(0x6001, list_device[1].product_id);

  ASSERT_TRUE(policy.GetAutoLaunchedKioskAppId(&string_value));
  EXPECT_EQ("my_kiosk_app", string_value);

  int_value = -1;
  ASSERT_TRUE(policy.GetSecondFactorAuthenticationMode(&int_value));
  EXPECT_EQ(2, int_value);

  std::vector<DevicePolicy::WeeklyTimeInterval> intervals;
  ASSERT_TRUE(policy.GetDisallowedTimeIntervals(&intervals));
  ASSERT_EQ(2, intervals.size());
  EXPECT_EQ(4, intervals[0].start_day_of_week);
  EXPECT_EQ(base::Minutes(30) + base::Hours(12), intervals[0].start_time);
  EXPECT_EQ(6, intervals[0].end_day_of_week);
  EXPECT_EQ(base::Minutes(15) + base::Hours(3), intervals[0].end_time);
  EXPECT_EQ(1, intervals[1].start_day_of_week);
  EXPECT_EQ(base::Minutes(10) + base::Hours(20), intervals[1].start_time);
  EXPECT_EQ(3, intervals[1].end_day_of_week);
  EXPECT_EQ(base::Minutes(20), intervals[1].end_time);

  base::Version device_minimum_version;
  const base::Version expected_minimum_version("13315.60.12");
  ASSERT_TRUE(policy.GetHighestDeviceMinimumVersion(&device_minimum_version));
  EXPECT_EQ(expected_minimum_version, device_minimum_version);

  // Reloading the protobuf should succeed.
  EXPECT_TRUE(provider.Reload());
}

// Test the deprecated usb_detachable_whitelist using a copy of the test policy
// data and removing the usb_detachable_allowlist.
TEST_F(LibpolicyTest, DevicePolicyWhitelistTest) {
  const enterprise_management::ChromeDeviceSettingsProto policy_data_value =
      CreateFullySetPolicyDataValue();
  const auto& policy_file = GetTmpDirPath().Append("policy");
  const auto& key_file = GetTmpDirPath().Append("key");
  ASSERT_TRUE(SignAndPersist(policy_data_value, policy_file, key_file));

  PolicyProvider provider;
  provider.SetDevicePolicyForTesting(CreateDevicePolicyImpl(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeEnterprise, true),
      policy_file, key_file, false));
  provider.Reload();

  // Ensure we successfully loaded the device policy file.
  ASSERT_TRUE(provider.device_policy_is_loaded());

  enterprise_management::ChromeDeviceSettingsProto proto =
      static_cast<const DevicePolicyImpl&>(provider.GetDevicePolicy())
          .get_device_policy_for_testing();
  proto.clear_usb_detachable_allowlist();
  ASSERT_FALSE(proto.has_usb_detachable_allowlist());
  ASSERT_TRUE(proto.has_usb_detachable_whitelist());

  DevicePolicyImpl device_policy;
  device_policy.set_policy_for_testing(proto);

  std::vector<DevicePolicy::UsbDeviceId> list_device;
  ASSERT_TRUE(device_policy.GetUsbDetachableWhitelist(&list_device));
  ASSERT_EQ(1, list_device.size());
  EXPECT_EQ(0x01d1, list_device[0].vendor_id);
  EXPECT_EQ(0xdead, list_device[0].product_id);
}

// Test that a policy file can be verified and parsed correctly. The file
// contains none of the possible fields, so reading should fail for all.
TEST_F(LibpolicyTest, DevicePolicyNoneSetTest) {
  const enterprise_management::ChromeDeviceSettingsProto empty_policy_data;
  const auto& policy_file = GetTmpDirPath().Append("policy");
  const auto& key_file = GetTmpDirPath().Append("key");
  ASSERT_TRUE(SignAndPersist(empty_policy_data, policy_file, key_file));

  PolicyProvider provider;
  provider.SetDevicePolicyForTesting(CreateDevicePolicyImpl(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeEnterprise, true),
      policy_file, key_file, false));
  provider.Reload();

  // Ensure we successfully loaded the device policy file.
  ASSERT_TRUE(provider.device_policy_is_loaded());

  const DevicePolicy& policy = provider.GetDevicePolicy();

  // Check that we cannot read any fields out of the sample protobuf.
  int int_value;
  int64_t int64_value;
  bool bool_value;
  std::string string_value;
  std::vector<DevicePolicy::UsbDeviceId> list_device;
  std::vector<DevicePolicy::WeeklyTimeInterval> intervals;
  base::Version device_minimum_version;
  DevicePolicy::EphemeralSettings ephemeral_settings;

  EXPECT_FALSE(policy.GetPolicyRefreshRate().has_value());
  EXPECT_FALSE(policy.GetCameraEnabled(&bool_value));
  EXPECT_FALSE(policy.GetShowUserNames(&bool_value));
  EXPECT_FALSE(policy.GetDataRoamingEnabled(&bool_value));
  EXPECT_FALSE(policy.GetAllowNewUsers(&bool_value));
  EXPECT_THAT(policy.GetMetricsEnabled(), testing::Optional(true));
  EXPECT_FALSE(policy.GetReportVersionInfo(&bool_value));
  EXPECT_FALSE(policy.GetUnenrolledHwDataUsageEnabled().has_value());
  // DeviceFlexHwDataForProductImprovementEnabled defaults to true,
  // so failure to read is success.
  EXPECT_TRUE(policy.GetEnrolledHwDataUsageEnabled().has_value());
  EXPECT_TRUE(*policy.GetEnrolledHwDataUsageEnabled());
  EXPECT_FALSE(policy.GetReportSystemInfo(&bool_value));
  EXPECT_FALSE(policy.GetReportCpuInfo(&bool_value));
  EXPECT_FALSE(policy.GetReportGraphicsStatus(&bool_value));
  EXPECT_FALSE(policy.GetReportMemoryInfo(&bool_value));
  EXPECT_FALSE(policy.GetReportNetworkConfig(&bool_value));
  EXPECT_FALSE(policy.GetReportActivityTimes(&bool_value));
  EXPECT_FALSE(policy.GetReportBootMode(&bool_value));
  EXPECT_FALSE(policy.GetEphemeralSettings(&ephemeral_settings));
  EXPECT_FALSE(policy.GetReleaseChannel(&string_value));
  EXPECT_FALSE(policy.GetDeviceExtendedAutoUpdateEnabled().has_value());
  EXPECT_FALSE(policy.GetUpdateDisabled(&bool_value));
  EXPECT_FALSE(policy.GetTargetVersionPrefix(&string_value));
  EXPECT_FALSE(policy.GetRollbackToTargetVersion(&int_value));
  // RollbackAllowedMilestones has the default value of 4 for enterprise
  // devices.
  ASSERT_TRUE(policy.GetRollbackAllowedMilestones(&int_value));
  EXPECT_EQ(4, int_value);
  EXPECT_FALSE(policy.GetScatterFactorInSeconds(&int64_value));
  EXPECT_FALSE(policy.GetOpenNetworkConfiguration(&string_value));
  EXPECT_FALSE(policy.GetHttpDownloadsEnabled(&bool_value));
  EXPECT_FALSE(policy.GetAuP2PEnabled(&bool_value));
  EXPECT_FALSE(policy.GetAllowKioskAppControlChromeVersion(&bool_value));
  EXPECT_FALSE(policy.GetUsbDetachableWhitelist(&list_device));
  EXPECT_FALSE(policy.GetSecondFactorAuthenticationMode(&int_value));
  EXPECT_FALSE(policy.GetDisallowedTimeIntervals(&intervals));
  EXPECT_FALSE(policy.GetHighestDeviceMinimumVersion(&device_minimum_version));
}

// Verify that the library will correctly recognize and signal missing files.
TEST_F(LibpolicyTest, DevicePolicyFailure) {
  LOG(INFO) << "Errors expected.";
  // Try loading non-existing protobuf should fail.
  base::FilePath policy_file(kNonExistingFile);
  base::FilePath key_file(kNonExistingFile);
  PolicyProvider provider;
  provider.SetDevicePolicyForTesting(
      CreateDevicePolicyImpl(std::make_unique<MockInstallAttributesReader>(
                                 cryptohome::SerializedInstallAttributes()),
                             policy_file, key_file, true));

  // Even after reload the policy should still be not loaded.
  ASSERT_FALSE(provider.Reload());
  EXPECT_FALSE(provider.device_policy_is_loaded());
}

// Ensure that signature verification is enforced for a device in vanilla
// enterprise mode.
TEST_F(LibpolicyTest, DontSkipSignatureForEnterprise) {
  const enterprise_management::ChromeDeviceSettingsProto
      empty_policy_data_value;
  const auto& policy_file = GetTmpDirPath().Append("policy");
  const auto& key_file = GetTmpDirPath().Append("key");
  ASSERT_TRUE(SignAndPersist(empty_policy_data_value, policy_file, key_file));
  ASSERT_TRUE(brillo::DeleteFile(key_file));

  PolicyProvider provider;
  provider.SetDevicePolicyForTesting(CreateDevicePolicyImpl(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeEnterprise, true),
      policy_file, key_file, false));
  provider.Reload();

  // Ensure that unverifed policy is not loaded.
  EXPECT_FALSE(provider.device_policy_is_loaded());
}

// Ensure that signature verification is enforced for a device in consumer mode.
TEST_F(LibpolicyTest, DontSkipSignatureForConsumer) {
  const enterprise_management::ChromeDeviceSettingsProto
      empty_policy_data_value;
  const auto& policy_file = GetTmpDirPath().Append("policy");
  const auto& key_file = GetTmpDirPath().Append("key");
  ASSERT_TRUE(SignAndPersist(empty_policy_data_value, policy_file, key_file));
  ASSERT_TRUE(brillo::DeleteFile(key_file));

  cryptohome::SerializedInstallAttributes install_attributes;
  PolicyProvider provider;
  provider.SetDevicePolicyForTesting(CreateDevicePolicyImpl(
      std::make_unique<MockInstallAttributesReader>(install_attributes),
      policy_file, key_file, false));
  provider.Reload();

  // Ensure that unverifed policy is not loaded.
  EXPECT_FALSE(provider.device_policy_is_loaded());
}

// Checks return value of IsConsumerDevice when it's a still in OOBE.
TEST_F(LibpolicyTest, DeviceInOobeIsNotConsumerOwned) {
  PolicyProvider provider;
  provider.SetInstallAttributesReaderForTesting(
      std::make_unique<MockInstallAttributesReader>("", false));
  EXPECT_FALSE(provider.IsConsumerDevice());
}

// Checks return value of IsConsumerDevice when it's a consumer device.
TEST_F(LibpolicyTest, ConsumerDeviceIsConsumerOwned) {
  PolicyProvider provider;
  provider.SetInstallAttributesReaderForTesting(
      std::make_unique<MockInstallAttributesReader>("", true));
  EXPECT_TRUE(provider.IsConsumerDevice());
}

// Checks return value of IsConsumerDevice when it's an enterprise device.
TEST_F(LibpolicyTest, EnterpriseDeviceIsNotConsumerOwned) {
  PolicyProvider provider;
  provider.SetInstallAttributesReaderForTesting(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeEnterprise, true));
  EXPECT_FALSE(provider.IsConsumerDevice());
}

TEST_F(LibpolicyTest, LegacyKioskDeviceIsNotConsumerOwned) {
  PolicyProvider provider;
  provider.SetInstallAttributesReaderForTesting(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeLegacyRetail, true));
  EXPECT_FALSE(provider.IsConsumerDevice());
}

TEST_F(LibpolicyTest, ConsumerKioskDeviceIsConsumerOwned) {
  PolicyProvider provider;
  provider.SetInstallAttributesReaderForTesting(
      std::make_unique<MockInstallAttributesReader>(
          InstallAttributesReader::kDeviceModeConsumerKiosk, true));
  EXPECT_TRUE(provider.IsConsumerDevice());
}

}  // namespace policy
