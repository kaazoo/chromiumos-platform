// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SHILL_PROXY_INTERFACE_H_
#define MINIOS_SHILL_PROXY_INTERFACE_H_

#include <base/callback.h>
#include <brillo/errors/error.h>
#include <brillo/variant_dictionary.h>

#include "minios/shill_utils.h"

namespace minios {

class ShillProxyInterface {
 public:
  virtual ~ShillProxyInterface() = default;

  using OnManagerRequestScanSuccess = base::Callback<void()>;
  using OnManagerRequestScanError = base::Callback<void(brillo::Error*)>;
  virtual void ManagerRequestScan(const WifiTechnologyType& technology,
                                  OnManagerRequestScanSuccess success_callback,
                                  OnManagerRequestScanError error_callback) = 0;

  using OnManagerGetPropertiesSuccess =
      base::Callback<void(const brillo::VariantDictionary&)>;
  using OnManagerGetPropertiesError = base::Callback<void(brillo::Error*)>;
  virtual void ManagerGetProperties(
      OnManagerGetPropertiesSuccess success_callback,
      OnManagerGetPropertiesError error_callback) = 0;

  using OnManagerFindMatchingServiceSuccess =
      base::Callback<void(const dbus::ObjectPath&)>;
  using OnManagerFindMatchingServiceError =
      base::Callback<void(brillo::Error*)>;
  virtual void ManagerFindMatchingService(
      const brillo::VariantDictionary& dict,
      OnManagerFindMatchingServiceSuccess success_callback,
      OnManagerFindMatchingServiceError error_callback) = 0;

  using OnServiceGetPropertiesSuccess =
      base::Callback<void(const brillo::VariantDictionary&)>;
  using OnServiceGetPropertiesError = base::Callback<void(brillo::Error*)>;
  virtual void ServiceGetProperties(
      const dbus::ObjectPath& service_path,
      OnServiceGetPropertiesSuccess success_callback,
      OnServiceGetPropertiesError error_callback) = 0;

  using OnServiceSetPropertiesSuccess = base::Callback<void()>;
  using OnServiceSetPropertiesError = base::Callback<void(brillo::Error*)>;
  virtual void ServiceSetProperties(
      const dbus::ObjectPath& service_path,
      const brillo::VariantDictionary& dict,
      OnServiceSetPropertiesSuccess success_callback,
      OnServiceSetPropertiesError error_callback) = 0;

  using OnServiceConnectSuccess = base::Callback<void()>;
  using OnServiceConnectError = base::Callback<void(brillo::Error*)>;
  virtual void ServiceConnect(const dbus::ObjectPath& service_path,
                              OnServiceConnectSuccess success_callback,
                              OnServiceConnectError error_callback) = 0;
};

}  // namespace minios

#endif  // MINIOS_SHILL_PROXY_INTERFACE_H__
