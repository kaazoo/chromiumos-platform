// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <base/base64.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/system/sys_info.h>
#include <base/values.h>
#include <brillo/file_utils.h>
#include <brillo/process/process.h>
#include <rootdev/rootdev.h>
#include <vpd/vpd.h>

#include "libsegmentation/device_info.pb.h"
#include "libsegmentation/feature_management_hwid.h"
#include "libsegmentation/feature_management_impl.h"
#include "libsegmentation/feature_management_interface.h"
#include "libsegmentation/feature_management_util.h"

namespace segmentation {

namespace {

// The path for the "gsctool" binary.
const char kGscToolBinaryPath[] = "/usr/sbin/gsctool";

// The output of |kGscToolBinaryPath| will contain a "chassis_x_branded:" line.
const char kChassisXBrandedKey[] = "chassis_x_branded:";

// The output of |kGscToolBinaryPath| will contain a "hw_compliance_version:"
// line.
const char kHwXComplianceVersion[] = "hw_x_compliance_version:";

// The output from the "gsctool" binary. Some or all of these fields may not be
// present in the output.
struct GscToolOutput {
  bool chassis_x_branded;
  int32_t hw_compliance_version;
};

// Parses output from running |kGscToolBinaryPath| into GscToolOutput.
std::optional<GscToolOutput> ParseGscToolOutput(
    const std::string& gsc_tool_output) {
  GscToolOutput output;
  std::istringstream iss(gsc_tool_output);
  std::string line;

  // Flags to indicate we've found the required fields.
  bool found_chassis = false;
  bool found_compliance_version = false;

  // Keep going till there are lines in the output or we've found both the
  // fields.
  while (std::getline(iss, line) &&
         (!found_chassis || !found_compliance_version)) {
    std::istringstream line_stream(line);
    std::string key;
    line_stream >> key;

    if (key == kChassisXBrandedKey) {
      bool value;
      line_stream >> std::boolalpha >> value;
      output.chassis_x_branded = value;
      found_chassis = true;
    } else if (key == kHwXComplianceVersion) {
      int32_t value;
      line_stream >> value;
      output.hw_compliance_version = value;
      found_compliance_version = true;
    }
  }

  if (found_chassis && found_compliance_version) {
    return output;
  }
  return std::nullopt;
}

// Returns the device information parsed from the output of the GSC tool binary
// on the device.
std::optional<GscToolOutput> GetDeviceInfoFromGSC() {
  CHECK(base::PathExists(base::FilePath(kGscToolBinaryPath)));

  base::FilePath output_path;
  if (!base::CreateTemporaryFile(&output_path)) {
    LOG(ERROR) << "Failed to open output file";
    return std::nullopt;
  }

  brillo::ProcessImpl process;
  process.AddArg(kGscToolBinaryPath);
  std::vector<std::string> args = {"--factory_config", "--any"};
  for (const auto& arg : args) {
    process.AddArg(arg);
  }
  process.RedirectOutput(output_path);

  if (!process.Start()) {
    LOG(ERROR) << "Failed to start gsctool process";
    return std::nullopt;
  }

  if (process.Wait() < 0) {
    LOG(ERROR) << "Failed to wait for the gsctool process";
    return std::nullopt;
  }

  std::string output;
  if (!base::ReadFileToString(output_path, &output)) {
    LOG(ERROR) << "Failed to read output from the gsctool";
    return std::nullopt;
  }

  std::optional<GscToolOutput> gsc_tool_output = ParseGscToolOutput(output);
  if (!gsc_tool_output) {
    LOG(ERROR) << "Failed to parse output from the gsctool";
    return std::nullopt;
  }

  return gsc_tool_output;
}
}  // namespace

FeatureManagementInterface::FeatureLevel
FeatureManagementImpl::GetFeatureLevel() {
  if (cached_device_info_) {
    return FeatureManagementUtil::ConvertProtoFeatureLevel(
        cached_device_info_->feature_level());
  }

  if (!CacheDeviceInfo()) {
    return FeatureLevel::FEATURE_LEVEL_UNKNOWN;
  }

  return FeatureManagementUtil::ConvertProtoFeatureLevel(
      cached_device_info_->feature_level());
}

FeatureManagementInterface::ScopeLevel FeatureManagementImpl::GetScopeLevel() {
  if (cached_device_info_) {
    return FeatureManagementUtil::ConvertProtoScopeLevel(
        cached_device_info_->scope_level());
  }

  if (!CacheDeviceInfo()) {
    return ScopeLevel::SCOPE_LEVEL_UNKNOWN;
  }

  return FeatureManagementUtil::ConvertProtoScopeLevel(
      cached_device_info_->scope_level());
}

bool FeatureManagementImpl::CacheDeviceInfo() {
  std::optional<libsegmentation::DeviceInfo> device_info_result;

  base::FilePath tpmfs_cache = base::FilePath(kTempDeviceInfoPath);
  // Read from the tmpfs file if it exists.
  if (base::PathExists(tpmfs_cache)) {
    device_info_result = FeatureManagementUtil::ReadDeviceInfo(tpmfs_cache);
    // To overwrite hash check: it eases testing and prevent entering the real
    // logic.
    if (device_info_result) {
      device_info_result->set_cached_version_hash(current_version_hash_);
    }
  }
  // No luck from tmpfs, read from the cached location in vpd.
  if (!device_info_result) {
    std::optional<std::string> encoded =
        vpd_->GetValue(vpd::VpdRw, kVpdKeyDeviceInfo);
    if (encoded) {
      device_info_result = FeatureManagementUtil::ReadDeviceInfo(*encoded);
    }
  }

  // If the device info isn't cached, read it form the hardware id and write it
  // to tpmfs for subsequent calls until reboots.
  // A upstart job may save the value in the VPD when the device is stable.
  if (!device_info_result ||
      device_info_result->cached_version_hash() != current_version_hash_) {
    // If we are running in a VM, do not check HWID/GSC.
    std::optional<int> inside_vm =
        crossystem_->VbGetSystemPropertyInt("inside_vm");
    if (!inside_vm || *inside_vm) {
      LOG(WARNING) << "Skip HIWD/GSC checking inside VM.";
      return false;
    }

    std::optional<GscToolOutput> gsc_tool_output = GetDeviceInfoFromGSC();
    if (!gsc_tool_output) {
      LOG(ERROR) << "Failed to get device info from the hardware id";
      return false;
    }

    FeatureManagementHwid::GetDeviceSelectionFn get_device_callback =
        [this](bool check) { return this->GetDeviceInfoFromHwid(check); };
    device_info_result = FeatureManagementHwid::GetDeviceInfo(
        get_device_callback, gsc_tool_output->chassis_x_branded,
        gsc_tool_output->hw_compliance_version);
    device_info_result->set_cached_version_hash(current_version_hash_);

    // Write in the tmpfs cache. Do not write in the VPD since the API call
    // could be done early at boot, in a time-critical section. It will be
    // written later in the VPD by a call to "feature_check --flash".
    if (!brillo::WriteStringToFile(
            tpmfs_cache,
            FeatureManagementUtil::EncodeDeviceInfo(*device_info_result))) {
      LOG(ERROR) << "Failed to cache device info in " << tpmfs_cache.value();
      return false;
    }
  }

  // At this point device information is present on stateful. We can cache it.
  cached_device_info_ = device_info_result.value();
  return true;
}

std::optional<DeviceSelection> FeatureManagementImpl::GetDeviceInfoFromHwid(
    bool check_prefix_only) {
  std::optional<std::string> hwid =
      crossystem_->VbGetSystemPropertyString("hwid");
  if (!hwid) {
    LOG(ERROR) << "Unable to retrieve HWID";
    return std::nullopt;
  }
  std::optional<DeviceSelection> selection =
      FeatureManagementHwid::GetSelectionFromHWID(
          selection_bundle_, hwid.value(), check_prefix_only);
  if (!selection) {
    return std::nullopt;
  }

  if (!check_prefix_only && !Check_HW_Requirement(selection.value())) {
    LOG(ERROR) << hwid.value() << " do not meet feature level "
               << selection->feature_level() << " requirement.";
    return std::nullopt;
  }
  return selection;
}

bool FeatureManagementImpl::Check_HW_Requirement(
    const DeviceSelection& selection) {
  if (selection.feature_level() == 0) {
    LOG(ERROR) << "Unexpected feature level: 0";
    return false;
  }

  if (selection.feature_level() > 1) {
    LOG(ERROR) << "Requirement not defined yet for feature_level "
               << selection.feature_level();
    return false;
  }

  // Feature level 1:
  // DRAM >= 8GiB. But since not all the physical RAM is available (PCI hole),
  // settle for 7GiB.
  // Obtain the size of the physical memory of the system.
  const size_t k7GiB = 7 * 1024 * 1024 * 1024ULL;
  if (base::SysInfo::AmountOfPhysicalMemory() < k7GiB) {
    return false;
  }

  // SSD >= 128GB
  // But since SSD counts in power of 10 and controller may even take a bigger
  // share, settle for 110GiB.
  // sysinfo AmountOfTotalDiskSpace can not be used, it returns the size of the
  // underlying filesystem.
  std::optional<base::FilePath> root_device =
      FeatureManagementUtil::GetDefaultRoot(base::FilePath("/"));
  if (!root_device) {
    return false;
  }

  std::optional<int64_t> size =
      FeatureManagementUtil::GetDiskSpace(*root_device);
  if (!size) {
    return false;
  }

  const size_t k110GiB = 110 * 1024 * 1024 * 1024ULL;
  if (*size < k110GiB) {
    return false;
  }

  return true;
}

bool FeatureManagementImpl::FlashLevels() {
  base::FilePath tpmfs_cache = base::FilePath(kTempDeviceInfoPath);
  if (!base::PathExists(tpmfs_cache)) {
    // Usual case: the VPD is up to date, CacheDeviceInfo() did not have to
    // query the device internals.
    VLOG(1) << "Segmentation level has not been computed since boot.";
    return true;
  }

  std::string encoded_cached;
  if (!base::ReadFileToString(tpmfs_cache, &encoded_cached)) {
    LOG(WARNING) << "Unable to read cached value";
    return false;
  }

  std::optional<std::string> encoded_saved =
      vpd_->GetValue(vpd::VpdRw, kVpdKeyDeviceInfo);
  if (!encoded_saved || encoded_saved != encoded_cached) {
    LOG(INFO) << "Update VPD information";
    return vpd_->WriteValue(vpd::VpdRw, kVpdKeyDeviceInfo, encoded_cached);
  }

  // What CacheDeviceInfo() calculated ended up being the same as the
  // one in the VPD. It can happen during testing.
  LOG(INFO) << "VPD already up to date";
  return true;
}

}  // namespace segmentation
