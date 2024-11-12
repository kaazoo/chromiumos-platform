// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ENCRYPTION_ENCRYPTION_MODULE_INTERFACE_H_
#define MISSIVE_ENCRYPTION_ENCRYPTION_MODULE_INTERFACE_H_

#include <atomic>
#include <string_view>

#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/time/time.h>

#include "missive/proto/record.pb.h"
#include "missive/util/dynamic_flag.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

constexpr char kEncryptionKeyFilePrefix[] = "EncryptionKey.";
constexpr int32_t kEncryptionKeyMaxFileSize = 256;

// Default period to refresh encryption key.
constexpr base::TimeDelta kDefaultKeyRefreshPeriod = base::Days(3);

class EncryptionModuleInterface
    : public DynamicFlag,
      public base::RefCountedThreadSafe<EncryptionModuleInterface> {
 public:
  // Public key id, as defined by Keystore.
  using PublicKeyId = int32_t;

  explicit EncryptionModuleInterface(
      bool is_enabled,
      base::TimeDelta renew_encryption_key_period = kDefaultKeyRefreshPeriod);
  EncryptionModuleInterface(const EncryptionModuleInterface& other) = delete;
  EncryptionModuleInterface& operator=(const EncryptionModuleInterface& other) =
      delete;

  // EncryptRecord will attempt to encrypt the provided |record| and respond
  // with the callback. On success the returned EncryptedRecord will contain
  // the encrypted string and encryption information. EncryptedRecord then can
  // be further updated by the caller.
  void EncryptRecord(
      std::string_view record,
      base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb) const;

  // Records current public asymmetric key. Makes a not about last update time.
  void UpdateAsymmetricKey(std::string_view new_public_key,
                           PublicKeyId new_public_key_id,
                           base::OnceCallback<void(Status)> response_cb);

  // Returns `false` if encryption key has not been set yet, and `true`
  // otherwise. The result is lazy: the method may return `false` for some time
  // even after the key has already been set - this is harmless, since resetting
  // or even changing the key is OK at any time.
  bool has_encryption_key() const;

  // Returns `true` if encryption key has not been set yet or it is too old
  // (received more than |renew_encryption_key_period| ago).
  bool need_encryption_key() const;

 protected:
  ~EncryptionModuleInterface() override;

 private:
  friend base::RefCountedThreadSafe<EncryptionModuleInterface>;

  // Implements EncryptRecord for the actual module.
  virtual void EncryptRecordImpl(
      std::string_view record,
      base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb) const = 0;

  // Implements UpdateAsymmetricKey for the actual module.
  virtual void UpdateAsymmetricKeyImpl(
      std::string_view new_public_key,
      PublicKeyId new_public_key_id,
      base::OnceCallback<void(Status)> response_cb) = 0;

  // Timestamp of the last public asymmetric key update by
  // |UpdateAsymmetricKey|. Initial value base::TimeTicks() indicates key is not
  // set yet.
  std::atomic<base::TimeTicks> last_encryption_key_update_{base::TimeTicks()};

  // Period of encryption key update.
  const base::TimeDelta renew_encryption_key_period_;
};

}  // namespace reporting

#endif  // MISSIVE_ENCRYPTION_ENCRYPTION_MODULE_INTERFACE_H_
