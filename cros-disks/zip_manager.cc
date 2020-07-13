// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/zip_manager.h"

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/scoped_mount_namespace.h>

#include "cros-disks/error_logger.h"
#include "cros-disks/fuse_helper.h"
#include "cros-disks/fuse_mounter.h"
#include "cros-disks/metrics.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"

namespace cros_disks {

ZipManager::~ZipManager() {
  UnmountAll();
}

bool ZipManager::CanMount(const std::string& source_path) const {
  // Check for expected file extension.
  return base::EndsWith(source_path, ".zip",
                        base::CompareCase::INSENSITIVE_ASCII) &&
         IsInAllowedFolder(source_path);
}

std::unique_ptr<MountPoint> ZipManager::DoMount(
    const std::string& source_path,
    const std::string& /*filesystem_type*/,
    const std::vector<std::string>& options,
    const base::FilePath& mount_path,
    MountOptions* const applied_options,
    MountErrorType* const error) {
  DCHECK(applied_options);
  DCHECK(error);

  metrics()->RecordArchiveType("zip");

  FUSEMounter::Params params{
      .bind_paths = {{source_path}},
      .filesystem_type = "zipfs",
      .metrics = metrics(),
      .metrics_name = "FuseZip",
      .mount_group = FUSEHelper::kFilesGroup,
      .mount_program = "/usr/bin/fuse-zip",
      .mount_user = "fuse-zip",
      .password_needed_code = 36,  // ZIP_ER_BASE + ZIP_ER_NOPASSWD
      .platform = platform(),
      .process_reaper = process_reaper(),
      .seccomp_policy = "/usr/share/policy/fuse-zip-seccomp.policy",
  };

  // Prepare FUSE mount options.
  {
    uid_t uid;
    gid_t gid;
    if (!platform()->GetUserAndGroupId(FUSEHelper::kFilesUser, &uid, nullptr) ||
        !platform()->GetGroupId(FUSEHelper::kFilesGroup, &gid)) {
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }

    params.mount_options.WhitelistOptionPrefix("umask=");
    params.mount_options.Initialize(
        {"umask=0222", MountOptions::kOptionReadOnly}, true,
        base::NumberToString(uid), base::NumberToString(gid));

    *applied_options = params.mount_options;
  }

  // Determine which mount namespace to use.
  {
    // Attempt to enter the Chrome mount namespace, if it exists.
    auto guard = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(kChromeMountNamespacePath));

    // Check if the source path exists in Chrome's mount namespace.
    if (guard && base::PathExists(base::FilePath(source_path))) {
      // The source path exists in Chrome's mount namespace.
      params.mount_namespace = kChromeMountNamespacePath;
    }
  }

  // To access Play Files.
  {
    gid_t gid;
    if (params.platform->GetGroupId("android-everybody", &gid))
      params.supplementary_groups.push_back(gid);
  }

  // Run fuse-zip.
  const FUSEMounter mounter(std::move(params));
  return mounter.Mount(source_path, mount_path, options, error);
}

}  // namespace cros_disks
