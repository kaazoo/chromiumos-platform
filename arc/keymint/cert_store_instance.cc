// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/cert_store_instance.h"

#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/pending_remote.h>

namespace arc::keymint {

CertStoreInstance::CertStoreInstance(
    base::WeakPtr<KeyMintServer> keymint_server)
    : keymint_server_(keymint_server) {}

void CertStoreInstance::UpdatePlaceholderKeys(
    std::vector<mojom::ChromeOsKeyPtr> keys,
    UpdatePlaceholderKeysCallback callback) {
  if (keymint_server_) {
    keymint_server_->UpdateContextPlaceholderKeys(std::move(keys),
                                                  std::move(callback));
  } else {
    std::move(callback).Run(/*success=*/false);
  }
}

void CertStoreInstance::SetSerialNumber(const std::string& serial_number) {
  if (keymint_server_ == nullptr) {
    LOG(ERROR)
        << "Failed to set the ARCVM serial number. KeyMint Server is null.";
    return;
  }
  bool result = keymint_server_->SetSerialNumber(serial_number);
  if (!result) {
    LOG(ERROR) << "Failed to set the ARCVM serial number.";
    return;
  }
}

}  // namespace arc::keymint
