// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_arc_utils.h"

#include <vector>

#include "gtest/gtest.h"

namespace vm_tools {
namespace concierge {

namespace {

StartArcVmRequest CreateRequest(std::vector<std::string> disk_paths) {
  StartArcVmRequest request;
  for (const std::string& path : disk_paths) {
    auto* disk = request.add_disks();
    disk->set_path(path);
  }
  return request;
}

}  // namespace

TEST(ServiceArcUtilsTest, GetCryptohomePath) {
  EXPECT_EQ(GetCryptohomePath("deadbeef"),
            base::FilePath("/run/daemon-store/crosvm/deadbeef"));
}

TEST(ServiceArcUtilsTest, GetPstoreDest) {
  EXPECT_EQ(
      GetPstoreDest("deadbeef"),
      base::FilePath("/run/daemon-store/crosvm/deadbeef/YXJjdm0=.pstore"));
}

TEST(ServiceArcUtilsTest, GetVmmSwapUsageHistoryPath) {
  EXPECT_EQ(GetVmmSwapUsageHistoryPath("deadbeef"),
            base::FilePath(
                "/run/daemon-store/crosvm/deadbeef/arcvm.vmm_swap_history"));
}

TEST(ServiceArcUtilsTest, IsValidDemoImagePath) {
  EXPECT_TRUE(IsValidDemoImagePath(
      base::FilePath("/run/imageloader/demo-mode-resources/0.12.34.56/"
                     "android_demo_apps.squash")));

  // Invalid version string.
  EXPECT_TRUE(IsValidDemoImagePath(
      base::FilePath("/run/imageloader/demo-mode-resources/0..12.34.56/"
                     "android_demo_apps.squash")));

  // Invalid file name.
  EXPECT_FALSE(IsValidDemoImagePath(base::FilePath(
      "/run/imageloader/demo-mode-resources/0.12.34.56/invalid.squash")));
}

TEST(ServiceArcUtilsTest, IsValidDataImagePath) {
  // Valid concierge disk path.
  EXPECT_TRUE(IsValidDataImagePath(
      base::FilePath("/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.img")));

  // Invalid user hash.
  EXPECT_FALSE(IsValidDataImagePath(
      base::FilePath("/run/daemon-store/crosvm/invalid/YXJjdm0=.img")));

  // Invalid file name.
  EXPECT_FALSE(IsValidDataImagePath(
      base::FilePath("/run/daemon-store/crosvm/deadbeaf/invalid.img")));

  // Valid LVM block device path.
  EXPECT_TRUE(IsValidDataImagePath(
      base::FilePath("/dev/mapper/vm/dmcrypt-deadbeaf-arcvm")));

  // Invalid device name.
  EXPECT_FALSE(
      IsValidDataImagePath(base::FilePath("/dev/mapper/vm/invalid-arcvm")));
}

TEST(ServiceArcUtilsTest, IsValidMetadataImagePath) {
  // Valid metadata image path.
  EXPECT_TRUE(IsValidMetadataImagePath(base::FilePath(
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.metadata.img")));

  // Invalid user hash.
  EXPECT_FALSE(IsValidMetadataImagePath(base::FilePath(
      "/run/daemon-store/crosvm/invalid/YXJjdm0=.metadata.img")));

  // Invalid file name.
  EXPECT_FALSE(IsValidMetadataImagePath(base::FilePath(
      "/run/daemon-store/crosvm/deadbeaf/invalid.metadata.img")));
}

TEST(ServiceArcUtilsTest, IsValidPropertiesFileDiskPath) {
  // Valid metadata image path.
  EXPECT_TRUE(IsValidPropertiesFileDiskPath(base::FilePath(
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.runtime.prop")));

  // Invalid user hash.
  EXPECT_FALSE(IsValidPropertiesFileDiskPath(base::FilePath(
      "/run/daemon-store/crosvm/invalid/YXJjdm0=.runtime.prop")));

  // Invalid file name.
  EXPECT_FALSE(IsValidPropertiesFileDiskPath(base::FilePath(
      "/run/daemon-store/crosvm/deadbeaf/invalid.runtime.prop")));
}

TEST(ServiceArcUtilsTest, ValidateStartArcVmRequest) {
  constexpr char kValidDemoImagePath[] =
      "/run/imageloader/demo-mode-resources/0.12.34.56/"
      "android_demo_apps.squash";
  constexpr char kValidDataImagePath[] =
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.img";
  constexpr char kValidMetadataImagePath[] =
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.metadata.img";
  constexpr char kValidRuntimePropertiesFilePath[] =
      "/run/daemon-store/crosvm/deadbeaf/YXJjdm0=.runtime.prop";
  constexpr char kInvalidImagePath[] = "/opt/google/vms/android/invalid";

  // No disks.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest({})));

  // Only vendor image.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest({kVendorImagePath})));

  // Vendor image is invalid.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest({kInvalidImagePath})));

  // Vendor image is empty (not allowed).
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest({kEmptyDiskPath})));

  // With valid demo image.
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kValidDemoImagePath})));

  // With invalid demo image.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kInvalidImagePath})));

  // With empty demo image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath})));

  // With valid apex payload image.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kApexPayloadImagePath})));

  // With invalid apex payload image.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kInvalidImagePath})));

  // With empty apex payload image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath})));

  // With valid data image.
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kValidDataImagePath})));

  // With invalid data image.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath, kInvalidImagePath})));

  // With empty data image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath, kEmptyDiskPath})));

  // With valid metadata image.
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kValidMetadataImagePath})));

  // With invalid metadata image.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kInvalidImagePath})));

  // With empty metadata image (allowed).
  EXPECT_TRUE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kEmptyDiskPath})));

  // With valid system properties disk.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath, kEmptyDiskPath,
       kEmptyDiskPath, kValidRuntimePropertiesFilePath})));

  // With invalid system properties disk.
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kEmptyDiskPath, kInvalidImagePath})));

  // With empty system properties disk (not allowed).
  EXPECT_FALSE(ValidateStartArcVmRequest(
      CreateRequest({kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath,
                     kEmptyDiskPath, kEmptyDiskPath, kEmptyDiskPath})));

  // With 6 valid image paths.
  EXPECT_TRUE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kValidDemoImagePath, kApexPayloadImagePath,
       kValidDataImagePath, kValidMetadataImagePath,
       kValidRuntimePropertiesFilePath})));

  // Too many disks.
  EXPECT_FALSE(ValidateStartArcVmRequest(CreateRequest(
      {kVendorImagePath, kEmptyDiskPath, kEmptyDiskPath, kEmptyDiskPath,
       kEmptyDiskPath, kValidRuntimePropertiesFilePath, kEmptyDiskPath})));
}

TEST(ServiceArcUtilsTest, RelocateBootProps) {
  std::vector<std::string> params = {
      "root=/dev/vda",
      "init=/init",
      "androidboot.hardware=bertha",
      "androidboot.container=1",
      "androidboot.verifiedbootstate=orange",
      "androidboot.hardware.hardware=abc",
      "androidboothardware=cde",
  };

  std::vector<std::string> invalid_params = {
      "androidboot.hardware=bertha",
      "androidboot.foo",
      "androidboot.container=1",
  };

  std::string props;

  // Returns true for valid params
  EXPECT_TRUE(RelocateBootProps(&params, &props));

  auto vec_contains = [](const std::vector<std::string>& vec,
                         const std::string& term) {
    return std::find(vec.begin(), vec.end(), term) != vec.end();
  };

  auto str_contains = [](const std::string& str, const std::string& term) {
    return str.find(term) != std::string::npos;
  };

  // Non-androidboot. properties are left in |params|
  EXPECT_TRUE(vec_contains(params, "root=/dev/vda"));
  EXPECT_FALSE(str_contains(props, "/dev/vda"));
  EXPECT_TRUE(vec_contains(params, "init=/init"));
  EXPECT_FALSE(str_contains(props, "=/init"));
  EXPECT_TRUE(vec_contains(params, "androidboothardware=cde"));
  EXPECT_FALSE(str_contains(props, "=cde"));

  // Allowlisted properties are left in |params|
  EXPECT_TRUE(vec_contains(params, "androidboot.hardware=bertha"));
  EXPECT_FALSE(str_contains(props, "hardware=bertha"));
  EXPECT_TRUE(vec_contains(params, "androidboot.verifiedbootstate=orange"));
  EXPECT_FALSE(str_contains(props, "verifiedbootstate=orange"));

  // Android properties are moved to |props|
  EXPECT_FALSE(vec_contains(params, "androidboot.container=1"));
  EXPECT_TRUE(str_contains(props, "ro.boot.container=1"));

  // Property prefixed with an allowlisted property is moved to |props|
  EXPECT_FALSE(vec_contains(params, "androidboot.hardware.hardware=abc"));
  EXPECT_TRUE(str_contains(props, "ro.boot.hardware.hardware=abc"));

  // Returns false if any properties do not have values
  EXPECT_FALSE(RelocateBootProps(&invalid_params, &props));
}

}  // namespace concierge
}  // namespace vm_tools
