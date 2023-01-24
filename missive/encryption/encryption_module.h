// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_ENCRYPTION_ENCRYPTION_MODULE_H_
#define MISSIVE_ENCRYPTION_ENCRYPTION_MODULE_H_

#include <base/functional/callback.h>
#include <base/memory/ref_counted.h>
#include <base/strings/string_piece.h>
#include <base/time/time.h>

#include "missive/encryption/encryption.h"
#include "missive/encryption/encryption_module_interface.h"
#include "missive/proto/record.pb.h"
#include "missive/util/status.h"
#include "missive/util/statusor.h"

namespace reporting {

class EncryptionModule : public EncryptionModuleInterface {
 public:
  EncryptionModule(const EncryptionModule& other) = delete;
  EncryptionModule& operator=(const EncryptionModule& other) = delete;

  // Factory method creates |EncryptionModule| object.
  static scoped_refptr<EncryptionModuleInterface> Create(
      base::TimeDelta renew_encryption_key_period = base::Days(1));

 protected:
  // Constructor can only be called by |Create| factory method.
  explicit EncryptionModule(base::TimeDelta renew_encryption_key_period);

  ~EncryptionModule() override;

 private:
  friend base::RefCountedThreadSafe<EncryptionModule>;

  // Interface methods implementations.
  void EncryptRecordImpl(
      base::StringPiece record,
      base::OnceCallback<void(StatusOr<EncryptedRecord>)> cb) const override;

  void UpdateAsymmetricKeyImpl(
      base::StringPiece new_public_key,
      PublicKeyId new_public_key_id,
      base::OnceCallback<void(Status)> response_cb) override;

  // Encryptor.
  scoped_refptr<Encryptor> encryptor_;
};

}  // namespace reporting

#endif  // MISSIVE_ENCRYPTION_ENCRYPTION_MODULE_H_
