// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis_check.h"

#include <vector>

#include <base/files/file_util.h>
#include <base/files/important_file_writer.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>

namespace flex_hwis {

namespace {

enum class PolicyType {
  // Managed
  DeviceSystemInfo,
  DeviceCpuInfo,
  DeviceGraphicsStatus,
  DeviceMemoryInfo,
  DeviceVersionInfo,
  DeviceNetworkConfig,

  // Unmanaged
  HardwareDataUsage,
};

// Convert a PolicyType to a string for logging.
std::string PolicyTypeToString(PolicyType policy_type) {
  switch (policy_type) {
    case PolicyType::DeviceSystemInfo:
      return "DeviceSystemInfo";
    case PolicyType::DeviceCpuInfo:
      return "DeviceCpuInfo";
    case PolicyType::DeviceGraphicsStatus:
      return "DeviceGraphicsStatus";
    case PolicyType::DeviceMemoryInfo:
      return "DeviceMemoryInfo";
    case PolicyType::DeviceVersionInfo:
      return "DeviceVersionInfo";
    case PolicyType::DeviceNetworkConfig:
      return "DeviceNetworkConfig";
    case PolicyType::HardwareDataUsage:
      return "HardwareDataUsage";
  }
}

// Get the list of policies to check, depending on whether the device is
// enrolled or not.
std::vector<PolicyType> GetPolicyTypesToCheck(bool is_enterprise_enrolled) {
  if (is_enterprise_enrolled) {
    return {PolicyType::DeviceSystemInfo,     PolicyType::DeviceCpuInfo,
            PolicyType::DeviceGraphicsStatus, PolicyType::DeviceMemoryInfo,
            PolicyType::DeviceVersionInfo,    PolicyType::DeviceNetworkConfig};
  } else {
    return {PolicyType::HardwareDataUsage};
  }
}

// Read a device policy.
//
// If successfully retrieved, the policy value will be set in |val|.
//
// Returns true if the policy was successfully retrieved, or false if an
// error occurs.
bool ReadDevicePolicy(const policy::DevicePolicy& policy,
                      PolicyType policy_type,
                      bool* val) {
  switch (policy_type) {
    case PolicyType::DeviceSystemInfo:
      return policy.GetReportSystemInfo(val);
    case PolicyType::DeviceCpuInfo:
      return policy.GetReportCpuInfo(val);
    case PolicyType::DeviceGraphicsStatus:
      return policy.GetReportGraphicsStatus(val);
    case PolicyType::DeviceMemoryInfo:
      return policy.GetReportMemoryInfo(val);
    case PolicyType::DeviceVersionInfo:
      return policy.GetReportVersionInfo(val);
    case PolicyType::DeviceNetworkConfig:
      return policy.GetReportNetworkConfig(val);
    case PolicyType::HardwareDataUsage:
      return policy.GetHwDataUsageEnabled(val);
  }
}

std::optional<std::string> ReadAndTrimFile(const base::FilePath& file_path) {
  std::string out;
  if (!base::ReadFileToString(file_path, &out)) {
    return std::nullopt;
  }

  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out);
  return out;
}

// Check a single device policy to see whether it will deny permission
// for HWIS to send data.
//
// Returns true if the policy is successfully retrieved and the policies
// value is true. Returns false otherwise.
bool CheckPermissionForPolicy(const policy::DevicePolicy& policy,
                              PolicyType policy_type) {
  const std::string log_name = PolicyTypeToString(policy_type);
  bool policy_permission = false;

  if (!ReadDevicePolicy(policy, policy_type, &policy_permission)) {
    LOG(INFO) << log_name << " is not set";
    return false;
  }
  if (!policy_permission) {
    LOG(INFO) << "Hardware data not sent: " << log_name << " disabled.";
    return false;
  }
  return true;
}

int64_t NowToEpochInSeconds() {
  return (base::Time::Now() - base::Time::UnixEpoch()).InSeconds();
}

}  // namespace

constexpr char kDeviceNameFile[] = "var/lib/flex_hwis_tool/name";
constexpr char kHwisTimeStampFile[] = "var/lib/flex_hwis_tool/time";

FlexHwisCheck::FlexHwisCheck(const base::FilePath& base_path,
                             policy::PolicyProvider& provider)
    : base_path_(base_path), policy_provider_(provider) {}

std::optional<std::string> FlexHwisCheck::GetDeviceName() const {
  return ReadHwisFile(DeviceNamePath());
}

void FlexHwisCheck::DeleteDeviceName() {
  if (!brillo::DeleteFile(DeviceNamePath())) {
    LOG(INFO) << "Error deleting device name file";
  }
}

void FlexHwisCheck::SetDeviceName(const std::string_view name) {
  if (!WriteHwisFile(DeviceNamePath(), name)) {
    LOG(INFO) << "Error writing device name file";
  }
}

base::FilePath FlexHwisCheck::DeviceNamePath() const {
  return base_path_.Append(kDeviceNameFile);
}

std::optional<std::string> FlexHwisCheck::ReadHwisFile(
    const base::FilePath& file_path) const {
  std::optional<std::string> hwis_info;

  if (!(hwis_info = ReadAndTrimFile(file_path))) {
    LOG(INFO) << "Couldn't read flex_hwis file.";
    return std::nullopt;
  }
  if (hwis_info.value().empty()) {
    LOG(INFO) << "Read a blank flex_hwis file.";
    return std::nullopt;
  }

  return hwis_info;
}

bool FlexHwisCheck::WriteHwisFile(const base::FilePath& file_path,
                                  const std::string_view content) {
  if (base::CreateDirectory(file_path.DirName())) {
    return base::ImportantFileWriter::WriteFileAtomically(
        file_path, std::string(content) + "\n");
  }
  return false;
}

bool FlexHwisCheck::HasRunRecently() {
  std::optional<std::string> last_str;
  const base::FilePath file_path = base_path_.Append(kHwisTimeStampFile);
  if ((last_str = ReadHwisFile(file_path))) {
    int64_t last_from_epoch = 0;
    if (base::StringToInt64(last_str.value(), &last_from_epoch)) {
      // The service must wait at least 24 hours between sending hardware data.
      if ((NowToEpochInSeconds() - last_from_epoch) <
          base::Days(1).InSeconds()) {
        return true;
      }
    } else {
      LOG(INFO) << "Failed to convert timestamp: " << last_str.value()
                << " to integer.";
    }
  }
  return false;
}

void FlexHwisCheck::RecordSendTime() {
  const base::FilePath file_path = base_path_.Append(kHwisTimeStampFile);
  if (!(WriteHwisFile(file_path,
                      base::NumberToString(NowToEpochInSeconds())))) {
    LOG(INFO) << "Failed to write the timestamp";
  }
}

PermissionInfo FlexHwisCheck::CheckPermission() {
  PermissionInfo info;

  policy_provider_.Reload();
  if (!policy_provider_.device_policy_is_loaded()) {
    LOG(INFO) << "No device policy available on this device";
    return info;
  }
  info.loaded = true;

  const policy::DevicePolicy& policy = policy_provider_.GetDevicePolicy();
  info.managed = policy.IsEnterpriseEnrolled();

  // Deny permission if any one of the checked policies is disabled.
  info.permission = true;
  for (const auto policy_type : GetPolicyTypesToCheck(info.managed)) {
    if (!CheckPermissionForPolicy(policy, policy_type)) {
      info.permission = false;
    }
  }

  return info;
}
}  // namespace flex_hwis
