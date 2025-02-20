// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/policy_key.h"

#include <stdint.h>

#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <crypto/rsa_private_key.h>
#include <crypto/signature_verifier.h>

#include "login_manager/nss_util.h"
#include "login_manager/system_utils_impl.h"

namespace login_manager {

PolicyKey::PolicyKey(SystemUtils* system_utils,
                     const base::FilePath& key_file,
                     NssUtil* nss)
    : system_utils_(system_utils), key_file_(key_file), nss_(nss) {}

PolicyKey::~PolicyKey() {}

bool PolicyKey::Equals(const std::string& key_der) const {
  return VEquals(std::vector<uint8_t>(key_der.c_str(),
                                      key_der.c_str() + key_der.length()));
}

bool PolicyKey::VEquals(const std::vector<uint8_t>& key_der) const {
  return std::equal(key_.begin(), key_.end(), key_der.begin(), key_der.end());
}

bool PolicyKey::HaveCheckedDisk() const {
  return have_checked_disk_;
}

bool PolicyKey::IsPopulated() const {
  return !key_.empty();
}

bool PolicyKey::PopulateFromDiskIfPossible() {
  have_checked_disk_ = true;
  if (!base::PathExists(key_file_)) {
    LOG(INFO) << "No policy key on disk at " << key_file_;
    return true;
  }

  auto file_size = system_utils_->GetFileSize(key_file_);
  if (!file_size) {
    PLOG(ERROR) << "Failed to find a file at: " << key_file_;
    return false;
  }

  std::optional<std::vector<uint8_t>> buffer = base::ReadFileToBytes(key_file_);
  if (!buffer || buffer->size() != *file_size) {
    PLOG(ERROR) << key_file_ << " could not be read in its entirety!";
    return false;
  }

  if (!nss_->CheckPublicKeyBlob(*buffer)) {
    LOG(ERROR) << "Policy key " << key_file_ << " is corrupted!";
    return false;
  }
  key_ = std::move(buffer).value();
  return true;
}

bool PolicyKey::PopulateFromBuffer(const std::vector<uint8_t>& public_key_der) {
  if (!HaveCheckedDisk()) {
    LOG(WARNING) << "Haven't checked disk for owner key yet!";
    return false;
  }
  // Only get here if we've checked disk already.
  if (IsPopulated()) {
    LOG(ERROR) << "Already have an owner key!";
    return false;
  }
  // Only get here if we've checked disk AND we didn't load a key.
  key_ = public_key_der;
  return true;
}

bool PolicyKey::PopulateFromKeypair(crypto::RSAPrivateKey* pair) {
  std::vector<uint8_t> public_key_der;
  if (pair && pair->ExportPublicKey(&public_key_der)) {
    return PopulateFromBuffer(public_key_der);
  }
  LOG(ERROR) << "Failed to export public key from key pair";
  return false;
}

bool PolicyKey::Persist() {
  // It is a programming error to call this before checking for the key on disk.
  CHECK(HaveCheckedDisk()) << "Haven't checked disk for owner key yet!";
  if (!have_replaced_ && base::PathExists(key_file_)) {
    LOG(ERROR) << "Tried to overwrite owner key!";
    return false;
  }

  // Remove the key if it has been cleared.
  if (key_.empty()) {
    bool removed = system_utils_->RemoveFile(key_file_);
    PLOG_IF(ERROR, !removed) << "Failed to delete " << key_file_.value();
    return removed;
  }

  if (!system_utils_->AtomicFileWrite(key_file_,
                                      std::string(key_.begin(), key_.end()))) {
    PLOG(ERROR) << "Could not write data to " << key_file_.value();
    return false;
  }
  DLOG(INFO) << "wrote " << key_.size() << " bytes to " << key_file_.value();
  return true;
}

bool PolicyKey::Rotate(
    const std::vector<uint8_t>& public_key_der,
    const std::vector<uint8_t>& signature,
    const crypto::SignatureVerifier::SignatureAlgorithm algorithm) {
  if (!IsPopulated()) {
    LOG(ERROR) << "Don't yet have an owner key!";
    return false;
  }
  if (Verify(public_key_der, signature, algorithm)) {
    key_ = public_key_der;
    have_replaced_ = true;
    return true;
  }
  LOG(ERROR) << "Invalid signature on new key!";
  return false;
}

bool PolicyKey::ClobberCompromisedKey(
    const std::vector<uint8_t>& public_key_der) {
  // It is a programming error to call this before checking for the key on disk.
  CHECK(HaveCheckedDisk()) << "Haven't checked disk for owner key yet!";
  // It is a programming error to call this without a key already loaded.
  CHECK(IsPopulated()) << "Don't yet have an owner key!";

  key_ = public_key_der;
  return have_replaced_ = true;
}

bool PolicyKey::Verify(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& signature,
    const crypto::SignatureVerifier::SignatureAlgorithm algorithm) {
  if (!nss_->Verify(signature, data, key_, algorithm)) {
    LOG(ERROR) << "Signature verification failed";
    return false;
  }
  return true;
}

}  // namespace login_manager
