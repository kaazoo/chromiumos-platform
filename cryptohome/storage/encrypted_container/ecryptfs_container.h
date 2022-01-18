// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ECRYPTFS_CONTAINER_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ECRYPTFS_CONTAINER_H_

#include "cryptohome/storage/encrypted_container/encrypted_container.h"

#include <base/files/file_path.h>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/filesystem_key.h"
#include "cryptohome/storage/keyring/keyring.h"

namespace cryptohome {

// `EcryptfsContainer` is a file-level encrypted container which uses eCryptFs
// to encrypted the `backing_dir_`.
class EcryptfsContainer : public EncryptedContainer {
 public:
  EcryptfsContainer(const base::FilePath& backing_dir,
                    const FileSystemKeyReference& key_reference,
                    Platform* platform,
                    Keyring* keyring);
  ~EcryptfsContainer() = default;

  bool Setup(const FileSystemKey& encryption_key) override;
  bool Teardown() override;
  bool Exists() override;
  bool Purge() override;
  EncryptedContainerType GetType() override {
    return EncryptedContainerType::kEcryptfs;
  }
  bool SetLazyTeardownWhenUnused() override;
  base::FilePath GetBackingLocation() const override;

 private:
  const base::FilePath backing_dir_;
  FileSystemKeyReference key_reference_;
  Platform* platform_;
  Keyring* keyring_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_ECRYPTFS_CONTAINER_H_
