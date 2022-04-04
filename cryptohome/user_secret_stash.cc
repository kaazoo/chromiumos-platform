// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_secret_stash.h"

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/system/sys_info.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/aes.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>

#include <map>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_container.h"
#include "cryptohome/flatbuffer_schemas/user_secret_stash_payload.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/file_system_keyset.h"

using ::hwsec_foundation::AesGcmDecrypt;
using ::hwsec_foundation::AesGcmEncrypt;
using ::hwsec_foundation::CreateSecureRandomBlob;
using ::hwsec_foundation::kAesGcm256KeySize;
using ::hwsec_foundation::kAesGcmIVSize;
using ::hwsec_foundation::kAesGcmTagSize;

namespace cryptohome {

namespace {

// TODO(b/230069013): Add guidelines on how to update this version value and its
// documentation when we need it for the first time.
constexpr int kCurrentUssVersion = 1;

constexpr char kEnableUssExperimentFlagPath[] =
    "/var/lib/cryptohome/uss_enabled";
constexpr char kDisableUssExperimentFlagPath[] =
    "/var/lib/cryptohome/uss_disabled";

std::optional<bool>& GetUserSecretStashExperimentFlag() {
  // The static variable holding the overridden state. The default state is
  // nullopt, which fallbacks to the default enabled/disabled state.
  static std::optional<bool> uss_experiment_enabled;
  return uss_experiment_enabled;
}

std::optional<bool>& GetUserSecretStashExperimentOverride() {
  // The static variable holding the overridden state. The default state is
  // nullopt, which fallbacks to checking whether flag file exists.
  static std::optional<bool> uss_experiment_enabled;
  return uss_experiment_enabled;
}

bool EnableUserSecretStashExperimentFlagFileExists() {
  return base::PathExists(base::FilePath(kEnableUssExperimentFlagPath));
}

bool DisableUserSecretStashExperimentFlagFileExists() {
  return base::PathExists(base::FilePath(kDisableUssExperimentFlagPath));
}

// Loads the current OS version from the CHROMEOS_RELEASE_VERSION field in
// /etc/lsb-release. Returns an empty string on failure.
std::string GetCurrentOsVersion() {
  std::string version;
  if (!base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_VERSION",
                                         &version)) {
    return std::string();
  }
  return version;
}

// Extracts the file system keyset from the given USS payload. Returns nullopt
// on failure.
std::optional<FileSystemKeyset> GetFileSystemKeyFromPayload(
    const UserSecretStashPayload& uss_payload) {
  if (uss_payload.fek.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK";
    return std::nullopt;
  }
  if (uss_payload.fnek.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK";
    return std::nullopt;
  }
  if (uss_payload.fek_salt.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK salt";
    return std::nullopt;
  }
  if (uss_payload.fnek_salt.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK salt";
    return std::nullopt;
  }
  if (uss_payload.fek_sig.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FEK signature";
    return std::nullopt;
  }
  if (uss_payload.fnek_sig.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no FNEK signature";
    return std::nullopt;
  }
  if (uss_payload.chaps_key.empty()) {
    LOG(ERROR) << "UserSecretStashPayload has no Chaps key";
    return std::nullopt;
  }
  FileSystemKey file_system_key = {
      .fek = uss_payload.fek,
      .fnek = uss_payload.fnek,
      .fek_salt = uss_payload.fek_salt,
      .fnek_salt = uss_payload.fnek_salt,
  };
  FileSystemKeyReference file_system_key_reference = {
      .fek_sig = uss_payload.fek_sig,
      .fnek_sig = uss_payload.fnek_sig,
  };
  return FileSystemKeyset(std::move(file_system_key),
                          std::move(file_system_key_reference),
                          uss_payload.chaps_key);
}

// Converts the wrapped key block information from serializable structs
// (autogenerated by the Python script) into the mapping from wrapping_id to
// `UserSecretStash::WrappedKeyBlock`.
// Malformed and duplicate entries are logged and skipped.
std::map<std::string, UserSecretStash::WrappedKeyBlock>
GetKeyBlocksFromSerializableStructs(
    const std::vector<UserSecretStashWrappedKeyBlock>& serializable_blocks) {
  std::map<std::string, UserSecretStash::WrappedKeyBlock> key_blocks;

  for (const UserSecretStashWrappedKeyBlock& serializable_block :
       serializable_blocks) {
    if (serializable_block.wrapping_id.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty ID.";
      continue;
    }
    if (key_blocks.count(serializable_block.wrapping_id)) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with duplicate ID "
          << serializable_block.wrapping_id << ".";
      continue;
    }

    if (!serializable_block.encryption_algorithm.has_value()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unset algorithm";
      continue;
    }
    if (serializable_block.encryption_algorithm.value() !=
        UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "unknown algorithm: "
                   << static_cast<int>(
                          serializable_block.encryption_algorithm.value());
      continue;
    }

    if (serializable_block.encrypted_key.empty()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty encrypted key.";
      continue;
    }

    if (serializable_block.iv.empty()) {
      LOG(WARNING)
          << "Ignoring UserSecretStash wrapped key block with an empty IV.";
      continue;
    }

    if (serializable_block.gcm_tag.empty()) {
      LOG(WARNING) << "Ignoring UserSecretStash wrapped key block with an "
                      "empty AES-GCM tag.";
      continue;
    }

    UserSecretStash::WrappedKeyBlock key_block = {
        .encryption_algorithm = serializable_block.encryption_algorithm.value(),
        .encrypted_key = serializable_block.encrypted_key,
        .iv = serializable_block.iv,
        .gcm_tag = serializable_block.gcm_tag,
    };
    key_blocks.insert({serializable_block.wrapping_id, std::move(key_block)});
  }

  return key_blocks;
}

// Parses the USS container flatbuffer. On success, populates `ciphertext`,
// `iv`, `tag`, `wrapped_key_blocks`, `created_on_os_version`; on failure,
// returns false.
bool GetContainerFromFlatbuffer(
    const brillo::SecureBlob& flatbuffer,
    brillo::SecureBlob* ciphertext,
    brillo::SecureBlob* iv,
    brillo::SecureBlob* tag,
    std::map<std::string, UserSecretStash::WrappedKeyBlock>* wrapped_key_blocks,
    std::string* created_on_os_version) {
  std::optional<UserSecretStashContainer> deserialized =
      UserSecretStashContainer::Deserialize(flatbuffer);
  if (!deserialized.has_value()) {
    LOG(ERROR) << "Failed to deserialize UserSecretStashContainer";
    return false;
  }

  if (!deserialized.value().encryption_algorithm.has_value()) {
    LOG(ERROR) << "UserSecretStashContainer has no algorithm set";
    return false;
  }
  if (deserialized.value().encryption_algorithm.value() !=
      UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStashContainer uses unknown algorithm: "
               << static_cast<int>(deserialized->encryption_algorithm.value());
    return false;
  }

  if (deserialized.value().ciphertext.empty()) {
    LOG(ERROR) << "UserSecretStash has empty ciphertext";
    return false;
  }
  *ciphertext = deserialized.value().ciphertext;

  if (deserialized.value().iv.empty()) {
    LOG(ERROR) << "UserSecretStash has empty IV";
    return false;
  }
  if (deserialized.value().iv.size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash has IV of wrong length: "
               << deserialized.value().iv.size()
               << ", expected: " << kAesGcmIVSize;
    return false;
  }
  *iv = deserialized.value().iv;

  if (deserialized.value().gcm_tag.empty()) {
    LOG(ERROR) << "UserSecretStash has empty AES-GCM tag";
    return false;
  }
  if (deserialized.value().gcm_tag.size() != kAesGcmTagSize) {
    LOG(ERROR) << "UserSecretStash has AES-GCM tag of wrong length: "
               << deserialized.value().gcm_tag.size()
               << ", expected: " << kAesGcmTagSize;
    return false;
  }
  *tag = deserialized.value().gcm_tag;

  *wrapped_key_blocks = GetKeyBlocksFromSerializableStructs(
      deserialized.value().wrapped_key_blocks);

  *created_on_os_version = deserialized.value().created_on_os_version;

  return true;
}

std::optional<brillo::SecureBlob> UnwrapMainKeyFromBlocks(
    const std::map<std::string, UserSecretStash::WrappedKeyBlock>&
        wrapped_key_blocks,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  // Verify preconditions.
  if (wrapping_id.empty()) {
    NOTREACHED() << "Empty wrapping ID is passed for UserSecretStash main key "
                    "unwrapping.";
    return std::nullopt;
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key unwrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return std::nullopt;
  }

  // Find the wrapped key block.
  const auto wrapped_key_block_iter = wrapped_key_blocks.find(wrapping_id);
  if (wrapped_key_block_iter == wrapped_key_blocks.end()) {
    LOG(ERROR)
        << "UserSecretStash wrapped key block with the given ID not found.";
    return std::nullopt;
  }
  const UserSecretStash::WrappedKeyBlock& wrapped_key_block =
      wrapped_key_block_iter->second;

  // Verify the wrapped key block format. No NOTREACHED() checks here, since the
  // key block is a deserialization of the persisted blob.
  if (wrapped_key_block.encryption_algorithm !=
      UserSecretStashEncryptionAlgorithm::AES_GCM_256) {
    LOG(ERROR) << "UserSecretStash wrapped main key uses unknown algorithm: "
               << static_cast<int>(wrapped_key_block.encryption_algorithm)
               << ".";
    return std::nullopt;
  }
  if (wrapped_key_block.encrypted_key.empty()) {
    LOG(ERROR) << "UserSecretStash wrapped main key has empty encrypted key.";
    return std::nullopt;
  }
  if (wrapped_key_block.iv.size() != kAesGcmIVSize) {
    LOG(ERROR) << "UserSecretStash wrapped main key has IV of wrong length: "
               << wrapped_key_block.iv.size() << ", expected: " << kAesGcmIVSize
               << ".";
    return std::nullopt;
  }
  if (wrapped_key_block.gcm_tag.size() != kAesGcmTagSize) {
    LOG(ERROR)
        << "UserSecretStash wrapped main key has AES-GCM tag of wrong length: "
        << wrapped_key_block.gcm_tag.size() << ", expected: " << kAesGcmTagSize
        << ".";
    return std::nullopt;
  }

  // Attempt the unwrapping.
  brillo::SecureBlob main_key;
  if (!AesGcmDecrypt(wrapped_key_block.encrypted_key, /*ad=*/std::nullopt,
                     wrapped_key_block.gcm_tag, wrapping_key,
                     wrapped_key_block.iv, &main_key)) {
    LOG(ERROR) << "Failed to unwrap UserSecretStash main key";
    return std::nullopt;
  }
  return main_key;
}

}  // namespace

int UserSecretStashExperimentVersion() {
  return kCurrentUssVersion;
}

bool IsUserSecretStashExperimentEnabled() {
  // If the state is overridden by tests, return this value.
  if (GetUserSecretStashExperimentOverride().has_value())
    return GetUserSecretStashExperimentOverride().value();
  // Otherwise, defer to checking the flag file existence. The disable file
  // precedes the enable file.
  if (DisableUserSecretStashExperimentFlagFileExists()) {
    return false;
  }
  if (EnableUserSecretStashExperimentFlagFileExists()) {
    return true;
  }
  // Otherwise, check the flag set by UssExperimentConfigFetcher.
  // TODO(b/230069013): Before actual launching, only report metrics for the
  // result of this flag, and don't actually use its value.
  UssExperimentFlag result = UssExperimentFlag::kNotFound;
  std::optional<bool> flag = GetUserSecretStashExperimentFlag();
  if (flag.has_value()) {
    if (flag.value()) {
      result = UssExperimentFlag::kEnabled;
    } else {
      result = UssExperimentFlag::kDisabled;
    }
  }
  ReportUssExperimentFlag(result);
  return false;
}

void SetUserSecretStashExperimentFlag(bool enabled) {
  GetUserSecretStashExperimentFlag() = enabled;
}

void SetUserSecretStashExperimentForTesting(std::optional<bool> enabled) {
  GetUserSecretStashExperimentOverride() = enabled;
}

// static
std::unique_ptr<UserSecretStash> UserSecretStash::CreateRandom(
    const FileSystemKeyset& file_system_keyset) {
  std::string current_os_version = GetCurrentOsVersion();

  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(
      new UserSecretStash(file_system_keyset));
  stash->created_on_os_version_ = std::move(current_os_version);
  return stash;
}

// static
std::unique_ptr<UserSecretStash> UserSecretStash::FromEncryptedContainer(
    const brillo::SecureBlob& flatbuffer, const brillo::SecureBlob& main_key) {
  if (main_key.size() != kAesGcm256KeySize) {
    LOG(ERROR) << "The UserSecretStash main key is of wrong length: "
               << main_key.size() << ", expected: " << kAesGcm256KeySize;
    return nullptr;
  }

  brillo::SecureBlob ciphertext, iv, gcm_tag;
  std::map<std::string, WrappedKeyBlock> wrapped_key_blocks;
  std::string created_on_os_version;
  if (!GetContainerFromFlatbuffer(flatbuffer, &ciphertext, &iv, &gcm_tag,
                                  &wrapped_key_blocks,
                                  &created_on_os_version)) {
    // Note: the error is already logged.
    return nullptr;
  }

  return FromEncryptedPayload(ciphertext, iv, gcm_tag, wrapped_key_blocks,
                              created_on_os_version, main_key);
}

// static
std::unique_ptr<UserSecretStash> UserSecretStash::FromEncryptedPayload(
    const brillo::SecureBlob& ciphertext,
    const brillo::SecureBlob& iv,
    const brillo::SecureBlob& gcm_tag,
    const std::map<std::string, WrappedKeyBlock>& wrapped_key_blocks,
    const std::string& created_on_os_version,
    const brillo::SecureBlob& main_key) {
  brillo::SecureBlob serialized_uss_payload;
  if (!AesGcmDecrypt(ciphertext, /*ad=*/std::nullopt, gcm_tag, main_key, iv,
                     &serialized_uss_payload)) {
    LOG(ERROR) << "Failed to decrypt UserSecretStash payload";
    return nullptr;
  }

  std::optional<UserSecretStashPayload> uss_payload =
      UserSecretStashPayload::Deserialize(serialized_uss_payload);
  if (!uss_payload.has_value()) {
    LOG(ERROR) << "Failed to deserialize UserSecretStashPayload";
    return nullptr;
  }

  std::optional<FileSystemKeyset> file_system_keyset =
      GetFileSystemKeyFromPayload(uss_payload.value());
  if (!file_system_keyset.has_value()) {
    LOG(ERROR)
        << "UserSecretStashPayload has invalid file system keyset information";
    return nullptr;
  }

  std::map<std::string, brillo::SecureBlob> reset_secrets;
  for (const ResetSecretMapping& item : uss_payload.value().reset_secrets) {
    auto insertion_status =
        reset_secrets.insert({item.auth_factor_label, item.reset_secret});
    if (!insertion_status.second) {
      LOG(ERROR) << "UserSecretStashPayload contains multiple reset secrets "
                    "for label: "
                 << item.auth_factor_label;
    }
  }

  // Note: make_unique() wouldn't work due to the constructor being private.
  std::unique_ptr<UserSecretStash> stash(
      new UserSecretStash(file_system_keyset.value(), reset_secrets));
  stash->wrapped_key_blocks_ = wrapped_key_blocks;
  stash->created_on_os_version_ = created_on_os_version;
  return stash;
}

// static
std::unique_ptr<UserSecretStash>
UserSecretStash::FromEncryptedContainerWithWrappingKey(
    const brillo::SecureBlob& flatbuffer,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key,
    brillo::SecureBlob* main_key) {
  brillo::SecureBlob ciphertext, iv, gcm_tag;
  std::map<std::string, WrappedKeyBlock> wrapped_key_blocks;
  std::string created_on_os_version;
  if (!GetContainerFromFlatbuffer(flatbuffer, &ciphertext, &iv, &gcm_tag,
                                  &wrapped_key_blocks,
                                  &created_on_os_version)) {
    // Note: the error is already logged.
    return nullptr;
  }

  std::optional<brillo::SecureBlob> main_key_optional =
      UnwrapMainKeyFromBlocks(wrapped_key_blocks, wrapping_id, wrapping_key);
  if (!main_key_optional) {
    // Note: the error is already logged.
    return nullptr;
  }

  std::unique_ptr<UserSecretStash> stash =
      FromEncryptedPayload(ciphertext, iv, gcm_tag, wrapped_key_blocks,
                           created_on_os_version, *main_key_optional);
  if (!stash) {
    // Note: the error is already logged.
    return nullptr;
  }
  *main_key = *main_key_optional;
  return stash;
}

// static
brillo::SecureBlob UserSecretStash::CreateRandomMainKey() {
  return CreateSecureRandomBlob(kAesGcm256KeySize);
}

const FileSystemKeyset& UserSecretStash::GetFileSystemKeyset() const {
  return file_system_keyset_;
}

std::optional<brillo::SecureBlob> UserSecretStash::GetResetSecretForLabel(
    const std::string& label) const {
  const auto iter = reset_secrets_.find(label);
  if (iter == reset_secrets_.end()) {
    return std::nullopt;
  }
  return iter->second;
}

bool UserSecretStash::SetResetSecretForLabel(const std::string& label,
                                             const brillo::SecureBlob& secret) {
  const auto result = reset_secrets_.insert({label, secret});
  return result.second;
}

const std::string& UserSecretStash::GetCreatedOnOsVersion() const {
  return created_on_os_version_;
}

bool UserSecretStash::HasWrappedMainKey(const std::string& wrapping_id) const {
  return wrapped_key_blocks_.count(wrapping_id);
}

std::optional<brillo::SecureBlob> UserSecretStash::UnwrapMainKey(
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) const {
  return UnwrapMainKeyFromBlocks(wrapped_key_blocks_, wrapping_id,
                                 wrapping_key);
}

bool UserSecretStash::AddWrappedMainKey(
    const brillo::SecureBlob& main_key,
    const std::string& wrapping_id,
    const brillo::SecureBlob& wrapping_key) {
  // Verify preconditions.
  if (main_key.empty()) {
    NOTREACHED() << "Empty UserSecretStash main key is passed for wrapping.";
    return false;
  }
  if (wrapping_id.empty()) {
    NOTREACHED()
        << "Empty wrapping ID is passed for UserSecretStash main key wrapping.";
    return false;
  }
  if (wrapping_key.size() != kAesGcm256KeySize) {
    NOTREACHED() << "Wrong wrapping key size is passed for UserSecretStash "
                    "main key wrapping. Received: "
                 << wrapping_key.size() << ", expected " << kAesGcm256KeySize
                 << ".";
    return false;
  }

  // Protect from duplicate wrapping IDs.
  if (wrapped_key_blocks_.count(wrapping_id)) {
    LOG(ERROR) << "A UserSecretStash main key with the given wrapping_id "
                  "already exists.";
    return false;
  }

  // Perform the wrapping.
  WrappedKeyBlock wrapped_key_block;
  wrapped_key_block.encryption_algorithm =
      UserSecretStashEncryptionAlgorithm::AES_GCM_256;
  if (!AesGcmEncrypt(main_key, /*ad=*/std::nullopt, wrapping_key,
                     &wrapped_key_block.iv, &wrapped_key_block.gcm_tag,
                     &wrapped_key_block.encrypted_key)) {
    LOG(ERROR) << "Failed to wrap UserSecretStash main key.";
    return false;
  }

  wrapped_key_blocks_[wrapping_id] = std::move(wrapped_key_block);
  return true;
}

bool UserSecretStash::RemoveWrappedMainKey(const std::string& wrapping_id) {
  auto iter = wrapped_key_blocks_.find(wrapping_id);
  if (iter == wrapped_key_blocks_.end()) {
    LOG(ERROR) << "No UserSecretStash wrapped key block is found with the "
                  "given wrapping ID.";
    return false;
  }
  wrapped_key_blocks_.erase(iter);
  return true;
}

std::optional<brillo::SecureBlob> UserSecretStash::GetEncryptedContainer(
    const brillo::SecureBlob& main_key) {
  UserSecretStashPayload payload = {
      .fek = file_system_keyset_.Key().fek,
      .fnek = file_system_keyset_.Key().fnek,
      .fek_salt = file_system_keyset_.Key().fek_salt,
      .fnek_salt = file_system_keyset_.Key().fnek_salt,
      .fek_sig = file_system_keyset_.KeyReference().fek_sig,
      .fnek_sig = file_system_keyset_.KeyReference().fnek_sig,
      .chaps_key = file_system_keyset_.chaps_key(),
  };

  // Note: It can happen that the USS container is created with empty
  // |reset_secrets_| if no PinWeaver credentials are present yet.
  for (const auto& item : reset_secrets_) {
    const std::string& auth_factor_label = item.first;
    const brillo::SecureBlob& reset_secret = item.second;
    payload.reset_secrets.push_back(ResetSecretMapping{
        .auth_factor_label = auth_factor_label,
        .reset_secret = reset_secret,
    });
  }

  std::optional<brillo::SecureBlob> serialized_payload = payload.Serialize();
  if (!serialized_payload.has_value()) {
    LOG(ERROR) << "Failed to serialize UserSecretStashPayload";
    return std::nullopt;
  }

  brillo::SecureBlob tag, iv, ciphertext;
  if (!AesGcmEncrypt(serialized_payload.value(), /*ad=*/std::nullopt, main_key,
                     &iv, &tag, &ciphertext)) {
    LOG(ERROR) << "Failed to encrypt UserSecretStash";
    return std::nullopt;
  }

  UserSecretStashContainer container = {
      .encryption_algorithm = UserSecretStashEncryptionAlgorithm::AES_GCM_256,
      .ciphertext = ciphertext,
      .iv = iv,
      .gcm_tag = tag,
      .created_on_os_version = created_on_os_version_,
  };
  // Note: It can happen that the USS container is created with empty
  // |wrapped_key_blocks_| - they may be added later, when the user registers
  // the first credential with their cryptohome.
  for (const auto& item : wrapped_key_blocks_) {
    const std::string& wrapping_id = item.first;
    const UserSecretStash::WrappedKeyBlock& wrapped_key_block = item.second;
    container.wrapped_key_blocks.push_back(UserSecretStashWrappedKeyBlock{
        .wrapping_id = wrapping_id,
        .encryption_algorithm = wrapped_key_block.encryption_algorithm,
        .encrypted_key = wrapped_key_block.encrypted_key,
        .iv = wrapped_key_block.iv,
        .gcm_tag = wrapped_key_block.gcm_tag,
    });
  }

  std::optional<brillo::SecureBlob> serialized_contaner = container.Serialize();
  if (!serialized_contaner.has_value()) {
    LOG(ERROR) << "Failed to serialize UserSecretStashContainer";
    return std::nullopt;
  }
  return serialized_contaner.value();
}

UserSecretStash::UserSecretStash(
    const FileSystemKeyset& file_system_keyset,
    const std::map<std::string, brillo::SecureBlob>& reset_secrets)
    : file_system_keyset_(file_system_keyset), reset_secrets_(reset_secrets) {}

UserSecretStash::UserSecretStash(const FileSystemKeyset& file_system_keyset)
    : file_system_keyset_(file_system_keyset) {}

}  // namespace cryptohome
