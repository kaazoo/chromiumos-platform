// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_LOAD_OOBE_CONFIG_USB_H_
#define OOBE_CONFIG_LOAD_OOBE_CONFIG_USB_H_

#include "oobe_config/load_oobe_config_interface.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>

namespace oobe_config {

// An object of this class has the responsibility of loading the oobe config
// file from usb along with the enrollment domain.
class LoadOobeConfigUsb : public LoadOobeConfigInterface {
 public:
  LoadOobeConfigUsb(const base::FilePath& stateful_dir,
                    const base::FilePath& device_ids_dir);
  ~LoadOobeConfigUsb() = default;

  bool GetOobeConfigJson(std::string* config,
                         std::string* enrollment_domain) override;

  // Creates an instance of this object with default paths to stateful partition
  // and device ids;
  static std::unique_ptr<LoadOobeConfigUsb> CreateInstance();

 private:
  // Checks that all the files necessary for USB enrollment exist.
  bool CheckFilesExistence();

  // Verifies the hash of the public key on the stateful partition matches the
  // one in the TPM.
  bool VerifyPublicKey();

  // Verifies the signature of a file using the default and already verified
  // public key.
  bool VerifySignature(const base::FilePath& file,
                       const base::FilePath& signature);

  // Locates the USB device using the device path's signature file.
  bool LocateUsbDevice(base::FilePath* device_id);

  // Verifies the enrollment domain exist in the config file.
  bool VerifyEnrollmentDomainInConfig(const std::string& config,
                                      const std::string& enrollment_domain);

  // Mounts the stateful partition of the discovered USB device.
  bool MountUsbDevice(const base::FilePath& device_path,
                      const base::FilePath& mount_point);

  // Unmounts the USB device.
  bool UnmountUsbDevice(const base::FilePath& mount_point);

  base::FilePath stateful_;
  base::FilePath unencrypted_oobe_config_dir_;
  base::FilePath pub_key_file_;
  base::FilePath config_signature_file_;
  base::FilePath enrollment_domain_signature_file_;
  base::FilePath usb_device_path_signature_file_;
  base::FilePath device_ids_dir_;

  DISALLOW_COPY_AND_ASSIGN(LoadOobeConfigUsb);
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_LOAD_OOBE_CONFIG_USB_H_
