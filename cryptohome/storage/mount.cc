// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Mount.

#include "cryptohome/storage/mount.h"

#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include <map>
#include <memory>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/hash/sha1.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <brillo/cryptohome.h>
#include <brillo/process/process.h>
#include <brillo/scoped_umask.h>
#include <brillo/secure_blob.h>
#include <chromeos/constants/cryptohome.h>
#include <google/protobuf/util/message_differencer.h>

#include "cryptohome/crypto/secure_blob_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/dircrypto_data_migrator/migration_helper.h"
#include "cryptohome/dircrypto_util.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_utils.h"
#include "cryptohome/tpm.h"
#include "cryptohome/vault_keyset.h"
#include "cryptohome/vault_keyset.pb.h"

using base::FilePath;
using base::StringPrintf;
using brillo::BlobToString;
using brillo::SecureBlob;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::IsSanitizedUserName;
using brillo::cryptohome::home::kGuestUserName;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;
using google::protobuf::util::MessageDifferencer;

namespace {
constexpr bool __attribute__((unused)) MountUserSessionOOP() {
  return USE_MOUNT_OOP;
}

}  // namespace

namespace cryptohome {

const char kChapsUserName[] = "chaps";
const char kDefaultSharedAccessGroup[] = "chronos-access";

void StartUserFileAttrsCleanerService(cryptohome::Platform* platform,
                                      const std::string& username) {
  std::unique_ptr<brillo::Process> file_attrs =
      platform->CreateProcessInstance();

  file_attrs->AddArg("/sbin/initctl");
  file_attrs->AddArg("start");
  file_attrs->AddArg("--no-wait");
  file_attrs->AddArg("file_attrs_cleaner_tool");
  file_attrs->AddArg(
      base::StringPrintf("OBFUSCATED_USERNAME=%s", username.c_str()));

  if (file_attrs->Run() != 0)
    PLOG(WARNING) << "Error while running file_attrs_cleaner_tool";
}

Mount::Mount(Platform* platform, HomeDirs* homedirs)
    : default_user_(-1),
      chaps_user_(-1),
      default_group_(-1),
      default_access_group_(-1),
      system_salt_(),
      platform_(platform),
      homedirs_(homedirs),
      legacy_mount_(true),
      bind_mount_downloads_(true),
      mount_type_(MountType::NONE),
      dircrypto_migration_stopped_condition_(&active_dircrypto_migrator_lock_) {
}

Mount::Mount() : Mount(nullptr, nullptr) {}

Mount::~Mount() {
  if (IsMounted())
    UnmountCryptohome();
}

bool Mount::Init(bool use_init_namespace) {
  bool result = true;

  // Get the user id and group id of the default user
  if (!platform_->GetUserId(kDefaultSharedUser, &default_user_,
                            &default_group_)) {
    result = false;
  }

  // Get the user id of the chaps user.
  gid_t not_used;
  if (!platform_->GetUserId(kChapsUserName, &chaps_user_, &not_used)) {
    result = false;
  }

  // Get the group id of the default shared access group.
  if (!platform_->GetGroupId(kDefaultSharedAccessGroup,
                             &default_access_group_)) {
    result = false;
  }

  // One-time load of the global system salt (used in generating username
  // hashes)
  if (!homedirs_->GetSystemSalt(&system_salt_)) {
    LOG(ERROR) << "Failed to load or create the system salt";
    result = false;
  }

  mounter_.reset(new MountHelper(
      default_user_, default_group_, default_access_group_, system_salt_,
      legacy_mount_, bind_mount_downloads_, platform_));
  active_mounter_ = mounter_.get();

  //  cryptohome_namespace_mounter enters the Chrome mount namespace and mounts
  //  the user cryptohome in that mount namespace if the flags are enabled.
  //  Chrome mount namespace is created by session_manager. cryptohome knows
  //  the path at which this mount namespace is created and uses that path to
  //  enter it.
  if (!use_init_namespace) {
    std::unique_ptr<MountNamespace> chrome_mnt_ns =
        std::make_unique<MountNamespace>(
            base::FilePath(kUserSessionMountNamespacePath), platform_);

    out_of_process_mounter_.reset(new OutOfProcessMountHelper(
        system_salt_, std::move(chrome_mnt_ns), legacy_mount_,
        bind_mount_downloads_, platform_));
    active_mounter_ = out_of_process_mounter_.get();
  }

  return result;
}

MountError Mount::MountEphemeralCryptohome(const std::string& username) {
  username_ = username;
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username_, system_salt_);

  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Ephemeral cryptohome can't be mounted twice.
  CHECK(active_mounter_->CanPerformEphemeralMount());

  MountError error = MOUNT_ERROR_NONE;
  CryptohomeVault::Options vault_options = {
      .force_type = EncryptedContainerType::kEphemeral,
  };

  user_cryptohome_vault_ = homedirs_->GenerateCryptohomeVault(
      obfuscated_username, FileSystemKeyReference(), vault_options,
      /*is_pristine=*/true, &error);

  if (error != MOUNT_ERROR_NONE || !user_cryptohome_vault_) {
    LOG(ERROR) << "Failed to generate ephemeral vault with error=" << error;
    return error != MOUNT_ERROR_NONE ? error : MOUNT_ERROR_FATAL;
  }

  error = user_cryptohome_vault_->Setup(FileSystemKey(), /*create=*/true);
  if (error != MOUNT_ERROR_NONE) {
    LOG(ERROR) << "Failed to setup ephemeral vault with error=" << error;
    user_cryptohome_vault_.reset();
    return error;
  }

  if (!active_mounter_->PerformEphemeralMount(
          username, user_cryptohome_vault_->GetContainerBackingLocation())) {
    LOG(ERROR) << "PerformEphemeralMount() failed, aborting ephemeral mount";
    return MOUNT_ERROR_FATAL;
  }

  mount_type_ = MountType::EPHEMERAL;
  ignore_result(cleanup_runner.Release());

  return MOUNT_ERROR_NONE;
}

bool Mount::MountCryptohome(const std::string& username,
                            const FileSystemKeyset& file_system_keyset,
                            const Mount::MountArgs& mount_args,
                            bool is_pristine,
                            MountError* mount_error) {
  username_ = username;
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username_, system_salt_);

  if (!mounter_->EnsureUserMountPoints(username_)) {
    LOG(ERROR) << "Error creating mountpoint.";
    *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    return false;
  }

  CryptohomeVault::Options vault_options;
  if (mount_args.force_dircrypto) {
    // If dircrypto is forced, it's an error to mount ecryptfs home unless
    // we are migrating from ecryptfs.
    vault_options.block_ecryptfs = true;
  } else if (mount_args.create_as_ecryptfs) {
    vault_options.force_type = EncryptedContainerType::kEcryptfs;
  }

  vault_options.migrate = mount_args.to_migrate_from_ecryptfs;

  user_cryptohome_vault_ = homedirs_->GenerateCryptohomeVault(
      obfuscated_username, file_system_keyset.KeyReference(), vault_options,
      is_pristine, mount_error);
  if (*mount_error != MOUNT_ERROR_NONE) {
    return false;
  }

  mount_type_ = user_cryptohome_vault_->GetMountType();

  if (mount_type_ == MountType::NONE) {
    // TODO(dlunev): there should be a more proper error code set. CREATE_FAILED
    // is a temporary returned error to keep the behaviour unchanged while
    // refactoring.
    *mount_error = MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED;
    return false;
  }

  // Set up the cryptohome vault for mount.
  *mount_error =
      user_cryptohome_vault_->Setup(file_system_keyset.Key(), is_pristine);
  if (*mount_error != MOUNT_ERROR_NONE) {
    return false;
  }

  // Ensure we don't leave any mounts hanging on intermediate errors.
  // The closure won't outlive the class so |this| will always be valid.
  // |out_of_process_mounter_|/|mounter_| will always be valid since this
  // callback runs in the destructor at the latest.
  base::ScopedClosureRunner cleanup_runner(base::BindOnce(
      base::IgnoreResult(&Mount::UnmountCryptohome), base::Unretained(this)));

  // Mount cryptohome
  // /home/.shadow: owned by root
  // /home/.shadow/$hash: owned by root
  // /home/.shadow/$hash/vault: owned by root
  // /home/.shadow/$hash/mount: owned by root
  // /home/.shadow/$hash/mount/root: owned by root
  // /home/.shadow/$hash/mount/user: owned by chronos
  // /home/chronos: owned by chronos
  // /home/chronos/user: owned by chronos
  // /home/user/$hash: owned by chronos
  // /home/root/$hash: owned by root

  mount_point_ = GetUserMountDirectory(obfuscated_username);
  // Since Service::Mount cleans up stale mounts, we should only reach
  // this point if someone attempts to re-mount an in-use mount point.
  if (platform_->IsDirectoryMounted(mount_point_)) {
    LOG(ERROR) << "Mount point is busy: " << mount_point_.value();
    *mount_error = MOUNT_ERROR_FATAL;
    return false;
  }

  std::string key_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fek_sig);
  std::string fnek_signature =
      SecureBlobToHex(file_system_keyset.KeyReference().fnek_sig);

  MountHelper::Options mount_opts = {mount_type_,
                                     mount_args.to_migrate_from_ecryptfs};

  cryptohome::ReportTimerStart(cryptohome::kPerformMountTimer);
  if (!active_mounter_->PerformMount(mount_opts, username_, key_signature,
                                     fnek_signature, is_pristine,
                                     mount_error)) {
    LOG(ERROR) << "MountHelper::PerformMount failed, error = " << *mount_error;
    return false;
  }

  cryptohome::ReportTimerStop(cryptohome::kPerformMountTimer);

  // Once mount is complete, do a deferred teardown for on the vault.
  // The teardown occurs when the vault's containers has no references ie. no
  // mount holds the containers open.
  // This is useful if cryptohome crashes: on recovery, if cryptohome decides to
  // cleanup mounts, the underlying devices (in case of dm-crypt cryptohome)
  // will be automatically torn down.

  // TODO(sarthakkukreti): remove this in favor of using the session-manager
  // as the source-of-truth during crash recovery. That would allow us to
  // reconstruct the run-time state of cryptohome vault(s) at the time of crash.
  ignore_result(user_cryptohome_vault_->SetLazyTeardownWhenUnused());

  // At this point we're done mounting.
  ignore_result(cleanup_runner.Release());

  *mount_error = MOUNT_ERROR_NONE;

  user_cryptohome_vault_->ReportVaultEncryptionType();

  // Start file attribute cleaner service.
  StartUserFileAttrsCleanerService(platform_, obfuscated_username);

  // TODO(fqj,b/116072767) Ignore errors since unlabeled files are currently
  // still okay during current development progress.
  // Report the success rate of the restore SELinux context operation for user
  // directory to decide on the action on failure when we  move on to the next
  // phase in the cryptohome SELinux development, i.e. making cryptohome
  // enforcing.
  if (platform_->RestoreSELinuxContexts(
          GetUserDirectoryForUser(obfuscated_username), true /*recursive*/)) {
    ReportRestoreSELinuxContextResultForHomeDir(true);
  } else {
    ReportRestoreSELinuxContextResultForHomeDir(false);
    LOG(ERROR) << "RestoreSELinuxContexts("
               << GetUserDirectoryForUser(obfuscated_username) << ") failed.";
  }

  return true;
}

bool Mount::UnmountCryptohome() {
  // There should be no file access when unmounting.
  // Stop dircrypto migration if in progress.
  MaybeCancelActiveDircryptoMigrationAndWait();

  active_mounter_->UnmountAll();

  // Resetting the vault teardowns the enclosed containers if setup succeeded.
  user_cryptohome_vault_.reset();

  mount_type_ = MountType::NONE;

  return true;
}

void Mount::UnmountCryptohomeFromMigration() {
  active_mounter_->UnmountAll();

  // Resetting the vault teardowns the enclosed containers if setup succeeded.
  user_cryptohome_vault_.reset();

  mount_type_ = MountType::NONE;
}

bool Mount::IsMounted() const {
  return (mounter_ && mounter_->MountPerformed()) ||
         (out_of_process_mounter_ && out_of_process_mounter_->MountPerformed());
}

bool Mount::IsEphemeral() const {
  return mount_type_ == MountType::EPHEMERAL;
}

bool Mount::IsNonEphemeralMounted() const {
  return IsMounted() && !IsEphemeral();
}

bool Mount::OwnsMountPoint(const FilePath& path) const {
  return (mounter_ && mounter_->IsPathMounted(path)) ||
         (out_of_process_mounter_ &&
          out_of_process_mounter_->IsPathMounted(path));
}

FilePath Mount::GetUserDirectoryForUser(
    const std::string& obfuscated_username) const {
  return ShadowRoot().Append(obfuscated_username);
}

bool Mount::SetupChapsDirectory(const FilePath& dir) {
  // If the Chaps database directory does not exist, create it.
  if (!platform_->DirectoryExists(dir)) {
    if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
            dir, S_IRWXU | S_IRGRP | S_IXGRP, chaps_user_,
            default_access_group_)) {
      LOG(ERROR) << "Failed to create " << dir.value();
      return false;
    }
    return true;
  }
  return true;
}

std::string Mount::GetMountTypeString() const {
  switch (mount_type_) {
    case MountType::NONE:
      return "none";
    case MountType::ECRYPTFS:
      return "ecryptfs";
    case MountType::DIR_CRYPTO:
      return "dircrypto";
    case MountType::EPHEMERAL:
      return "ephemeral";
    case MountType::DMCRYPT:
      return "dmcrypt";
  }
  return "";
}

bool Mount::MigrateToDircrypto(
    const dircrypto_data_migrator::MigrationHelper::ProgressCallback& callback,
    MigrationType migration_type) {
  std::string obfuscated_username =
      SanitizeUserNameWithSalt(username_, system_salt_);
  FilePath temporary_mount =
      GetUserTemporaryMountDirectory(obfuscated_username);
  if (!IsMounted() || mount_type_ != MountType::DIR_CRYPTO ||
      !platform_->DirectoryExists(temporary_mount) ||
      !OwnsMountPoint(temporary_mount)) {
    LOG(ERROR) << "Not mounted for eCryptfs->dircrypto migration.";
    return false;
  }
  // Do migration.
  constexpr uint64_t kMaxChunkSize = 128 * 1024 * 1024;
  dircrypto_data_migrator::MigrationHelper migrator(
      platform_, temporary_mount, mount_point_,
      GetUserDirectoryForUser(obfuscated_username), kMaxChunkSize,
      migration_type);
  {  // Abort if already cancelled.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    if (is_dircrypto_migration_cancelled_)
      return false;
    CHECK(!active_dircrypto_migrator_);
    active_dircrypto_migrator_ = &migrator;
  }
  bool success = migrator.Migrate(callback);

  UnmountCryptohomeFromMigration();
  {  // Signal the waiting thread.
    base::AutoLock lock(active_dircrypto_migrator_lock_);
    active_dircrypto_migrator_ = nullptr;
    dircrypto_migration_stopped_condition_.Signal();
  }
  if (!success) {
    LOG(ERROR) << "Failed to migrate.";
    return false;
  }
  // Clean up.
  FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  if (!platform_->DeletePathRecursively(temporary_mount) ||
      !platform_->DeletePathRecursively(vault_path)) {
    LOG(ERROR) << "Failed to delete the old vault.";
    return false;
  }
  return true;
}

void Mount::MaybeCancelActiveDircryptoMigrationAndWait() {
  base::AutoLock lock(active_dircrypto_migrator_lock_);
  is_dircrypto_migration_cancelled_ = true;
  while (active_dircrypto_migrator_) {
    active_dircrypto_migrator_->Cancel();
    LOG(INFO) << "Waiting for dircrypto migration to stop.";
    dircrypto_migration_stopped_condition_.Wait();
    LOG(INFO) << "Dircrypto migration stopped.";
  }
}
}  // namespace cryptohome
