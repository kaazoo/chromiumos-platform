// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive-manager.h"

#include <linux/capability.h>

#include <base/file_path.h>
#include <base/logging.h>
#include <base/string_util.h>

#include "cros-disks/mount-info.h"
#include "cros-disks/mount-options.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed-process.h"
#include "cros-disks/system-mounter.h"

using std::string;
using std::vector;

namespace {

// Mapping from a base path to its corresponding path inside the AVFS mount.
struct AVFSPathMapping {
  const char* const base_path;
  const char* const avfs_path;
};

// Process capabilities required by the avfsd process:
//   CAP_SYS_ADMIN for mounting/unmounting filesystem
const uint64_t kAVFSMountProgramCapabilities = 1 << CAP_SYS_ADMIN;

// Number of components in a mount directory path. A mount directory is always
// created under /media/<sub type>/<mount dir>, so it always has 4 components
// in the path: '/', 'media', '<sub type>', '<mount dir>'
size_t kNumComponentsInMountDirectoryPath = 4;
const char kAVFSMountProgram[] = "/usr/bin/avfsd";
const char kAVFSRootDirectory[] = "/var/run/avfsroot";
const char kAVFSMediaDirectory[] = "/var/run/avfsroot/media";
const char kAVFSUserFileDirectory[] = "/var/run/avfsroot/user";
const char kMediaDirectory[] = "/media";
const char kUserFileDirectory[] = "/home/chronos/user/Downloads";
const AVFSPathMapping kAVFSPathMapping[] = {
  { kMediaDirectory, kAVFSMediaDirectory },
  { kUserFileDirectory, kAVFSUserFileDirectory },
};

}  // namespace

namespace cros_disks {

ArchiveManager::ArchiveManager(const string& mount_root,
                               Platform* platform)
    : MountManager(mount_root, platform) {
}

bool ArchiveManager::Initialize() {
  RegisterDefaultFileExtensions();
  return MountManager::Initialize();
}

bool ArchiveManager::StartSession(const string& user) {
  uid_t user_id = platform_->mount_user_id();
  gid_t group_id = platform_->mount_group_id();

  if (!platform_->CreateDirectory(kAVFSRootDirectory) ||
      !platform_->SetOwnership(kAVFSRootDirectory, user_id, group_id) ||
      !platform_->SetPermissions(kAVFSRootDirectory, S_IRWXU)) {
    platform_->RemoveEmptyDirectory(kAVFSRootDirectory);
    return false;
  }

  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kAVFSPathMapping); ++i) {
    const string& avfs_path = kAVFSPathMapping[i].avfs_path;
    if (!platform_->CreateDirectory(avfs_path) ||
        !platform_->SetOwnership(avfs_path, user_id, group_id) ||
        !platform_->SetPermissions(avfs_path, S_IRWXU) ||
        !MountAVFSPath(kAVFSPathMapping[i].base_path, avfs_path)) {
      StopSession(user);
      return false;
    }
  }
  return true;
}

bool ArchiveManager::StopSession(const string& user) {
  // Unmounts all mounted archives before unmounting AVFS mounts.
  bool all_unmounted = UnmountAll();
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kAVFSPathMapping); ++i) {
    const string& path = kAVFSPathMapping[i].avfs_path;
    if (!platform_->Unmount(path))
      all_unmounted = false;
    platform_->RemoveEmptyDirectory(path);
  }
  platform_->RemoveEmptyDirectory(kAVFSRootDirectory);
  return all_unmounted;
}

bool ArchiveManager::CanMount(const string& source_path) const {
  // The following paths can be mounted:
  //     /home/chronos/user/Downloads/...<file>
  //     /media/<dir>/<dir>/...<file>
  FilePath file_path(source_path);
  if (FilePath(kUserFileDirectory).IsParent(file_path))
    return true;

  if (FilePath(kMediaDirectory).IsParent(file_path)) {
    vector<FilePath::StringType> components;
    file_path.StripTrailingSeparators().GetComponents(&components);
    // e.g. components = { '/', 'media', 'removable', 'usb', 'doc.zip' }
    if (components.size() > kNumComponentsInMountDirectoryPath)
      return true;
  }
  return false;
}

MountErrorType ArchiveManager::DoMount(const string& source_path,
                                       const string& filesystem_type,
                                       const vector<string>& options,
                                       const string& mount_path) {
  CHECK(!source_path.empty()) << "Invalid source path argument";
  CHECK(!mount_path.empty()) << "Invalid mount path argument";

  string avfs_path = GetAVFSPath(source_path);
  if (avfs_path.empty() || !platform_->experimental_features_enabled()) {
    LOG(ERROR) << "Path '" << source_path << "' is not a supported archive";
    return kMountErrorUnsupportedArchive;
  }

  // Perform a bind mount from the archive path under the AVFS mount
  // to /media/archive/<archive name>.
  vector<string> extended_options = options;
  extended_options.push_back(MountOptions::kOptionBind);
  MountOptions mount_options;
  mount_options.Initialize(extended_options, false, "", "");
  SystemMounter mounter(avfs_path, mount_path, "", mount_options);
  return mounter.Mount();
}

MountErrorType ArchiveManager::DoUnmount(const string& path,
                                         const vector<string>& options) {
  CHECK(!path.empty()) << "Invalid path argument";
  // TODO(benchan): Extract error from low-level unmount operation.
  return platform_->Unmount(path) ? kMountErrorNone : kMountErrorUnknown;
}

string ArchiveManager::SuggestMountPath(const string& source_path) const {
  // Use the archive name to name the mount directory.
  FilePath base_name = FilePath(source_path).BaseName();
  return FilePath(mount_root_).Append(base_name).value();
}

bool ArchiveManager::IsFileExtensionSupported(
    const string& extension) const {
  return extensions_.find(extension) != extensions_.end();
}

void ArchiveManager::RegisterDefaultFileExtensions() {
  // TODO(benchan): Perhaps these settings can be read from a config file.
  RegisterFileExtension("zip");
}

void ArchiveManager::RegisterFileExtension(const string& extension) {
  extensions_.insert(extension);
}

string ArchiveManager::GetAVFSPath(const string& path) const {
  FilePath file_path(path);
  string extension = file_path.Extension();
  if (!extension.empty()) {
    // Strip the leading dot and convert the extension to lower case.
    extension.erase(0, 1);
    StringToLowerASCII(&extension);
  }

  if (IsFileExtensionSupported(extension)) {
    for (size_t i = 0; i < ARRAYSIZE_UNSAFE(kAVFSPathMapping); ++i) {
      FilePath base_path(kAVFSPathMapping[i].base_path);
      FilePath avfs_path(kAVFSPathMapping[i].avfs_path);
      if (base_path.AppendRelativePath(file_path, &avfs_path)) {
        return avfs_path.value() + "#";
      }
    }
  }
  return string();
}

bool ArchiveManager::MountAVFSPath(const string& base_path,
                                   const string& avfs_path) const {
  MountInfo mount_info;
  if (!mount_info.RetrieveFromCurrentProcess())
    return false;

  if (mount_info.HasMountPath(avfs_path)) {
    LOG(WARNING) << "Path '" << avfs_path << "' is already mounted.";
    return false;
  }

  SandboxedProcess mount_process;
  mount_process.AddArgument(kAVFSMountProgram);
  mount_process.AddArgument("-o");
  mount_process.AddArgument("ro,nodev,noexec,nosuid,modules=subdir,subdir=" +
                            base_path);
  mount_process.AddArgument(avfs_path);
  mount_process.SetCapabilities(kAVFSMountProgramCapabilities);
  mount_process.SetUserId(platform_->mount_user_id());
  mount_process.SetGroupId(platform_->mount_group_id());
  if (mount_process.Run() != 0 ||
      !mount_info.RetrieveFromCurrentProcess() ||
      !mount_info.HasMountPath(avfs_path)) {
    LOG(WARNING) << "Failed to mount '" << base_path << "' to '"
                 << avfs_path << "' via AVFS";
    return false;
  }

  LOG(INFO) << "Mounted '" << base_path << "' to '" << avfs_path
            << "' via AVFS";
  return true;
}

}  // namespace cros_disks
