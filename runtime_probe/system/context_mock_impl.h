// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/scoped_temp_dir.h>
#include <debugd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <shill/dbus-proxies.h>
#include <shill/dbus-proxy-mocks.h>

#include "runtime_probe/system/context.h"
#include "runtime_probe/system/helper_invoker_direct_impl.h"

namespace runtime_probe {

class ContextMockImpl : public Context {
 public:
  ContextMockImpl();
  ~ContextMockImpl() override;

  org::chromium::debugdProxyInterface* debugd_proxy() override {
    return &mock_debugd_proxy_;
  };

  HelperInvoker* helper_invoker() override { return &helper_invoker_direct_; }

  const base::FilePath& root_dir() override { return root_dir_; }

  org::chromium::debugdProxyMock* mock_debugd_proxy() {
    return &mock_debugd_proxy_;
  }

  org::chromium::flimflam::ManagerProxyInterface* shill_manager_proxy()
      override {
    return &mock_shill_manager_proxy_;
  }

  org::chromium::flimflam::ManagerProxyMock* mock_shill_manager_proxy() {
    return &mock_shill_manager_proxy_;
  }

  std::unique_ptr<org::chromium::flimflam::DeviceProxyInterface>
  CreateShillDeviceProxy(const dbus::ObjectPath& path) override;

  // Set up shill devices paths that will be returned by the shill manager
  // proxy, and devices properties that will be returned by the shill device
  // proxies.
  //
  // @param shill_devices: A map of <device path, device properties>
  // indicates the shill devices and properties of each device to be set.
  void SetShillProxies(
      const std::map<std::string, brillo::VariantDictionary>& shill_devices);

 private:
  testing::StrictMock<org::chromium::debugdProxyMock> mock_debugd_proxy_;
  testing::NiceMock<org::chromium::flimflam::ManagerProxyMock>
      mock_shill_manager_proxy_;
  HelperInvokerDirectImpl helper_invoker_direct_;

  // Used to create a temporary root directory.
  base::ScopedTempDir temp_dir_;
  base::FilePath root_dir_;

  // Map dbus::ObjectPath::value() to its corresponding mock shill device
  // properties.
  std::map<std::string, brillo::VariantDictionary>
      mock_shill_device_properties_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_SYSTEM_CONTEXT_MOCK_IMPL_H_
