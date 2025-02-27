// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/allow_conforming_usb_device_rule.h"

#include <libudev.h>

#include <cstring>
#include <memory>
#include <set>
#include <string>

#include <base/logging.h>
#include <brillo/errors/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <featured/fake_platform_features.h>
#include <gtest/gtest.h>
#include <permission_broker/rule.h>
#include <permission_broker/rule_test.h>
#include <permission_broker/rule_utils.h>
#include <permission_broker/udev_scopers.h>

#include "base/containers/contains.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_number_conversions.h"
#include "featured/feature_library.h"
#include "gmock/gmock.h"
#include "primary_io_manager/dbus-proxy-mocks.h"

using testing::_;
using testing::Invoke;
using testing::Return;

using std::set;
using std::string;

constexpr char kChromeboxPermissiveRestrictionsFlag[] =
    "CrOSLateBootChromeboxUsbPassthroughRestrictions";

namespace permission_broker {

using MockHandlePtr = org::chromium::PrimaryIoManagerProxyMock*;

class AllowConformingUsbDeviceRuleMockPolicy
    : public AllowConformingUsbDeviceRule {
 public:
  AllowConformingUsbDeviceRuleMockPolicy()
      : AllowConformingUsbDeviceRule(
            std::make_unique<org::chromium::PrimaryIoManagerProxyMock>()) {}
  AllowConformingUsbDeviceRuleMockPolicy(
      const AllowConformingUsbDeviceRuleMockPolicy&) = delete;
  AllowConformingUsbDeviceRuleMockPolicy& operator=(
      const AllowConformingUsbDeviceRuleMockPolicy&) = delete;

  ~AllowConformingUsbDeviceRuleMockPolicy() override = default;

  void SetMockedUsbAllowList(
      const std::vector<policy::DevicePolicy::UsbDeviceId>& allowed) {
    usb_allow_list_ = allowed;
  }

  void SetPlatformFeaturesForTesting(
      feature::PlatformFeaturesInterface* platform_features) {
    platform_features_ = platform_features;
  }

  void SetPlatformFeature(const std::string& feature, bool enabled) {
    dynamic_cast<feature::FakePlatformFeatures*>(platform_features_)
        ->SetEnabled(feature, enabled);
  }

  MockHandlePtr GetMockHandle() {
    return static_cast<MockHandlePtr>(
        AllowConformingUsbDeviceRule::GetHandle());
  }

 private:
  bool LoadPolicy() override { return true; }
};

class AllowConformingUsbDeviceRuleTest : public RuleTest {
 public:
  AllowConformingUsbDeviceRuleTest() = default;
  AllowConformingUsbDeviceRuleTest(const AllowConformingUsbDeviceRuleTest&) =
      delete;
  AllowConformingUsbDeviceRuleTest& operator=(
      const AllowConformingUsbDeviceRuleTest&) = delete;

  ~AllowConformingUsbDeviceRuleTest() override = default;

 protected:
  void SetUp() override {
    auto bus = base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options{});
    platform_features_ = std::make_unique<feature::FakePlatformFeatures>(bus);
    rule_.SetPlatformFeaturesForTesting(platform_features_.get());
    rule_.SetPlatformFeature(kChromeboxPermissiveRestrictionsFlag, false);

    ScopedUdevPtr udev(udev_new());
    ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev.get()));
    udev_enumerate_add_match_subsystem(enumerate.get(), "usb");
    udev_enumerate_scan_devices(enumerate.get());

    struct udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry,
                            udev_enumerate_get_list_entry(enumerate.get())) {
      const char* syspath = udev_list_entry_get_name(entry);
      ScopedUdevDevicePtr device(
          udev_device_new_from_syspath(udev.get(), syspath));
      EXPECT_TRUE(device.get());

      const char* devtype = udev_device_get_devtype(device.get());
      if (!devtype || strcmp(devtype, "usb_interface") != 0) {
        continue;
      }

      // udev_device_get_parent_*() does not take a reference on the returned
      // device, it is automatically unref'd with the parent
      udev_device* parent = udev_device_get_parent(device.get());
      EXPECT_TRUE(parent);
      devtype = udev_device_get_devtype(parent);
      if (!devtype || strcmp(devtype, "usb_device") != 0) {
        continue;
      }

      const char* devnode = udev_device_get_devnode(parent);
      if (!devnode) {
        continue;
      }

      const char* parent_removable =
          udev_device_get_property_value(parent, kCrosUsbLocation);
      string path = devnode;

      if (!parent_removable) {
        unmarked_devices_.insert(path);
      } else if (strcmp(parent_removable, "external") == 0) {
        external_devices_.insert(path);
      } else if (strcmp(parent_removable, "internal") == 0) {
        internal_devices_.insert(path);
      } else if (strcmp(parent_removable, "unknown") == 0) {
        unknown_devices_.insert(path);
      } else {
        unmarked_devices_.insert(path);
      }

      const char* vid = udev_device_get_sysattr_value(parent, "idVendor");
      const char* pid = udev_device_get_sysattr_value(parent, "idProduct");
      unsigned vendor_id, product_id;
      if (!vid || !base::HexStringToUInt(vid, &vendor_id)) {
        continue;
      }
      if (!pid || !base::HexStringToUInt(pid, &product_id)) {
        continue;
      }
      policy::DevicePolicy::UsbDeviceId id;
      id.vendor_id = vendor_id;
      id.product_id = product_id;

      const char* driver = udev_device_get_driver(device.get());
      if (!base::Contains(partially_claimed_devices_, path)) {
        if (driver) {
          auto it = unclaimed_devices_.find(path);
          if (it == unclaimed_devices_.end()) {
            claimed_devices_.insert(path);
            if (strcmp(driver, "hub") != 0) {
              detachable_allow_list_.push_back(id);
              detachable_devices_.insert(path);
            }
          } else {
            partially_claimed_devices_.insert(path);
            unclaimed_devices_.erase(it);
          }
        } else {
          auto it = claimed_devices_.find(path);
          if (it == claimed_devices_.end()) {
            unclaimed_devices_.insert(path);
          } else {
            partially_claimed_devices_.insert(path);
            claimed_devices_.erase(it);
          }
        }
      }
    }
  }

  void SetAllDevicesPrimary(bool primary) {
    MockHandlePtr handle = rule_.GetMockHandle();
    ON_CALL(*handle, IsPrimaryIoDevice(_, _, _, _))
        .WillByDefault(testing::Invoke(
            [primary](const std::string& in_device, bool* out_primary,
                      brillo::ErrorPtr* error, int timeout_ms) -> bool {
              *out_primary = primary;
              return true;
            }));
  }

  AllowConformingUsbDeviceRuleMockPolicy rule_;
  std::unique_ptr<feature::FakePlatformFeatures> platform_features_;
  set<string> external_devices_;
  set<string> internal_devices_;
  set<string> unknown_devices_;
  set<string> unmarked_devices_;

  set<string> claimed_devices_;
  set<string> unclaimed_devices_;
  set<string> partially_claimed_devices_;
  set<string> detachable_devices_;

  std::vector<policy::DevicePolicy::UsbDeviceId> detachable_allow_list_;
};

TEST_F(AllowConformingUsbDeviceRuleTest, Legacy_IgnoreNonUsbDevice) {
  SetAllDevicesPrimary(false);
  ASSERT_EQ(Rule::IGNORE, rule_.ProcessDevice(FindDevice("/dev/tty0").get()));
}

TEST_F(AllowConformingUsbDeviceRuleTest, Legacy_DenyClaimedUsbDevice) {
  SetAllDevicesPrimary(false);
  if (claimed_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no claimed devices "
                 << "connected.";
  }

  for (const string& device : claimed_devices_) {
    EXPECT_EQ(Rule::DENY, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

TEST_F(AllowConformingUsbDeviceRuleTest, Legacy_IgnoreUnclaimedUsbDevice) {
  SetAllDevicesPrimary(false);
  if (unclaimed_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no unclaimed devices "
                 << "connected.";
  }

  for (const string& device : unclaimed_devices_) {
    EXPECT_EQ(Rule::IGNORE, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

TEST_F(AllowConformingUsbDeviceRuleTest,
       Legacy_AllowPartiallyClaimedUsbDeviceWithLockdown) {
  SetAllDevicesPrimary(false);
  if (partially_claimed_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no partially claimed "
                 << "devices connected.";
  }

  for (const string& device : partially_claimed_devices_) {
    EXPECT_EQ(Rule::ALLOW_WITH_LOCKDOWN,
              rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

TEST_F(AllowConformingUsbDeviceRuleTest,
       Legacy_AllowDetachableClaimedUsbDevice) {
  SetAllDevicesPrimary(false);
  if (detachable_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no detachable "
                 << "devices connected.";
  }

  rule_.SetMockedUsbAllowList(detachable_allow_list_);

  for (const string& device : detachable_devices_) {
    EXPECT_EQ(Rule::ALLOW_WITH_DETACH,
              rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

TEST_F(AllowConformingUsbDeviceRuleTest, Tagged_AllowExternalDevices) {
  SetAllDevicesPrimary(false);
  if (external_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no external "
                 << "devices connected.";
  }

  for (const string& device : external_devices_) {
    EXPECT_EQ(Rule::ALLOW_WITH_DETACH,
              rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

TEST_F(AllowConformingUsbDeviceRuleTest, Tagged_DenyInternalDevices) {
  SetAllDevicesPrimary(false);
  if (internal_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no internal "
                 << "devices connected.";
  }

  for (const string& device : internal_devices_) {
    EXPECT_EQ(Rule::DENY, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

TEST_F(AllowConformingUsbDeviceRuleTest, Tagged_DenyUnknownDevices) {
  SetAllDevicesPrimary(false);
  if (unknown_devices_.empty()) {
    LOG(WARNING) << "Tests incomplete because there are no unknoen "
                 << "devices connected.";
  }

  for (const string& device : unknown_devices_) {
    EXPECT_EQ(Rule::DENY, rule_.ProcessDevice(FindDevice(device).get()))
        << device;
  }
}

}  // namespace permission_broker
