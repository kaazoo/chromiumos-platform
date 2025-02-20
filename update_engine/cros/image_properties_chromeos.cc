// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/cros/image_properties.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/key_value_store.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/hardware_interface.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/system_state.h"
#include "update_engine/common/utils.h"

namespace {

constexpr char kLsbRelease[] = "/etc/lsb-release";

constexpr char kLsbReleaseAppIdKey[] = "CHROMEOS_RELEASE_APPID";
constexpr char kLsbReleaseAutoUpdateServerKey[] = "CHROMEOS_AUSERVER";
constexpr char kLsbReleaseBoardAppIdKey[] = "CHROMEOS_BOARD_APPID";
constexpr char kLsbReleaseBoardKey[] = "CHROMEOS_RELEASE_BOARD";
constexpr char kLsbReleaseBuilderPath[] = "CHROMEOS_RELEASE_BUILDER_PATH";
constexpr char kLsbReleaseCanaryAppIdKey[] = "CHROMEOS_CANARY_APPID";
constexpr char kLsbReleaseIsPowerwashAllowedKey[] =
    "CHROMEOS_IS_POWERWASH_ALLOWED";
constexpr char kLsbReleaseUpdateChannelKey[] = "CHROMEOS_RELEASE_TRACK";
constexpr char kLsbReleaseVersionKey[] = "CHROMEOS_RELEASE_VERSION";

const char kDefaultAppId[] = "{87efface-864d-49a5-9bb3-4b050a7c227a}";

// A prefix added to the path, used for testing.
const char* root_prefix = nullptr;

std::string GetStringWithDefault(const brillo::KeyValueStore& store,
                                 const std::string& key,
                                 const std::string& default_value) {
  std::string result;
  if (store.GetString(key, &result)) {
    return result;
  }
  LOG(INFO) << "Cannot load ImageProperty " << key << ", using default value "
            << default_value;
  return default_value;
}

enum class LsbReleaseSource {
  kSystem,
  kStateful,
};

// Loads the lsb-release properties into the key-value |store| reading the file
// from either the system image or the stateful partition as specified by
// |source|. The loaded values are added to the store, possibly overriding
// existing values.
void LoadLsbRelease(LsbReleaseSource source, brillo::KeyValueStore* store) {
  std::string path;
  if (root_prefix) {
    path = root_prefix;
  }
  if (source == LsbReleaseSource::kStateful) {
    path += chromeos_update_engine::kStatefulPartition;
  }
  store->Load(base::FilePath(path + kLsbRelease));
}

}  // namespace

namespace chromeos_update_engine {

namespace test {
void SetImagePropertiesRootPrefix(const char* test_root_prefix) {
  root_prefix = test_root_prefix;
}
}  // namespace test

ImageProperties LoadImageProperties() {
  ImageProperties result;

  brillo::KeyValueStore lsb_release;
  LoadLsbRelease(LsbReleaseSource::kSystem, &lsb_release);
  result.current_channel = GetStringWithDefault(
      lsb_release, kLsbReleaseUpdateChannelKey, kStableChannel);

  // In dev-mode and unofficial build we can override the image properties set
  // in the system image with the ones from the stateful partition, except the
  // channel of the current image.
  HardwareInterface* const hardware = SystemState::Get()->hardware();
  if (!hardware->IsOfficialBuild() || !hardware->IsNormalBootMode()) {
    LoadLsbRelease(LsbReleaseSource::kStateful, &lsb_release);
  }

  // The release_app_id is used as the default appid, but can be override by
  // the board appid in the general case or the canary appid for the canary
  // channel only.
  std::string release_app_id =
      GetStringWithDefault(lsb_release, kLsbReleaseAppIdKey, kDefaultAppId);

  result.product_id = GetStringWithDefault(
      lsb_release, kLsbReleaseBoardAppIdKey, release_app_id);
  result.canary_product_id = GetStringWithDefault(
      lsb_release, kLsbReleaseCanaryAppIdKey, release_app_id);
  result.board = GetStringWithDefault(lsb_release, kLsbReleaseBoardKey, "");
  result.version = GetStringWithDefault(lsb_release, kLsbReleaseVersionKey, "");
  result.omaha_url =
      GetStringWithDefault(lsb_release, kLsbReleaseAutoUpdateServerKey,
                           constants::kOmahaDefaultProductionURL);
  result.builder_path =
      GetStringWithDefault(lsb_release, kLsbReleaseBuilderPath, "");
  // Build fingerprint not used in Chrome OS.
  result.build_fingerprint = "";
  result.allow_arbitrary_channels = false;

  return result;
}

MutableImageProperties LoadMutableImageProperties() {
  MutableImageProperties result;
  brillo::KeyValueStore lsb_release;
  LoadLsbRelease(LsbReleaseSource::kSystem, &lsb_release);
  LoadLsbRelease(LsbReleaseSource::kStateful, &lsb_release);
  result.target_channel = GetStringWithDefault(
      lsb_release, kLsbReleaseUpdateChannelKey, kStableChannel);
  if (!lsb_release.GetBoolean(kLsbReleaseIsPowerwashAllowedKey,
                              &result.is_powerwash_allowed)) {
    result.is_powerwash_allowed = false;
  }
  return result;
}

bool StoreMutableImageProperties(const MutableImageProperties& properties) {
  brillo::KeyValueStore lsb_release;
  LoadLsbRelease(LsbReleaseSource::kStateful, &lsb_release);
  lsb_release.SetString(kLsbReleaseUpdateChannelKey, properties.target_channel);
  lsb_release.SetBoolean(kLsbReleaseIsPowerwashAllowedKey,
                         properties.is_powerwash_allowed);

  std::string root_prefix_str = root_prefix ? root_prefix : "";
  base::FilePath path(root_prefix_str + kStatefulPartition + kLsbRelease);
  if (!base::DirectoryExists(path.DirName())) {
    base::CreateDirectory(path.DirName());
  }
  return lsb_release.Save(path);
}

void LogImageProperties() {
  std::string lsb_release;
  if (utils::ReadFile(kLsbRelease, &lsb_release)) {
    LOG(INFO) << "lsb-release inside the old rootfs:\n" << lsb_release;
  }

  std::string stateful_lsb_release;
  if (utils::ReadFile(std::string(kStatefulPartition) + kLsbRelease,
                      &stateful_lsb_release)) {
    LOG(INFO) << "stateful lsb-release:\n" << stateful_lsb_release;
  }
}

}  // namespace chromeos_update_engine
