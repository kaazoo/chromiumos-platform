// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/display/display_watcher.h"

#include <string>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/compiler_specific.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

#include "power_manager/powerd/system/udev_stub.h"
#include "power_manager/powerd/testing/test_environment.h"

namespace power_manager::system {

namespace {

// Stub implementation of DisplayWatcherObserver.
class TestObserver : public DisplayWatcherObserver {
 public:
  TestObserver() = default;
  TestObserver(const TestObserver&) = delete;
  TestObserver& operator=(const TestObserver&) = delete;

  ~TestObserver() override = default;

  int num_display_changes() const { return num_display_changes_; }

  // DisplayWatcherObserver implementation:
  void OnDisplaysChanged(const std::vector<DisplayInfo>& displays) override {
    num_display_changes_++;
  }

 private:
  // Number of times that OnDisplaysChanged() has been called.
  int num_display_changes_ = 0;
};

}  // namespace

class DisplayWatcherTest : public TestEnvironment {
 public:
  DisplayWatcherTest() {
    CHECK(drm_dir_.CreateUniqueTempDir());
    watcher_.set_sysfs_drm_path_for_testing(drm_dir_.GetPath());
    CHECK(device_dir_.CreateUniqueTempDir());
    watcher_.set_i2c_dev_path_for_testing(device_dir_.GetPath());
  }
  ~DisplayWatcherTest() override = default;

 protected:
  // Creates a directory named |device_name| in |device_dir_| and adds a symlink
  // to it in |drm_dir_|. Returns the path to the directory.
  base::FilePath CreateDrmDevice(const std::string& device_name) {
    base::FilePath device_path = device_dir_.GetPath().Append(device_name);
    CHECK(base::CreateDirectory(device_path));
    CHECK(base::CreateSymbolicLink(device_path,
                                   drm_dir_.GetPath().Append(device_name)));
    return device_path;
  }

  // Creates a directory named |device_name| in |device_dir_|. Returns the path
  // to the directory.
  base::FilePath CreateDevice(const std::string& device_name) {
    base::FilePath device_path = device_dir_.GetPath().Append(device_name);
    CHECK(base::CreateDirectory(device_path));
    return device_path;
  }

  // Adds a symlink to the parent device in the device's directory.
  void SetDeviceParent(const base::FilePath& device_path,
                       const base::FilePath& parent_path) {
    CHECK(base::CreateSymbolicLink(parent_path, device_path.Append("device")));
  }

  // Creates a file named |device_name| in |device_dir_|. Returns the path to
  // the file.
  base::FilePath CreateI2CDevice(const std::string& device_name) {
    base::FilePath device_path = device_dir_.GetPath().Append(device_name);
    CHECK(base::WriteFile(device_path, "\n"));
    return device_path;
  }

  // Notifies |watcher_| about a Udev event to trigger a rescan of displays.
  void NotifyAboutUdevEvent() {
    udev_.NotifySubsystemObservers(
        {{DisplayWatcher::kDrmUdevSubsystem, "devtype", "sysname", ""},
         UdevEvent::Action::CHANGE});
  }

  // Directory with symlinks to DRM devices.
  base::ScopedTempDir drm_dir_;

  // Directory holding device information symlinked to from the above
  // directories.
  base::ScopedTempDir device_dir_;

  UdevStub udev_;
  DisplayWatcher watcher_;
};

TEST_F(DisplayWatcherTest, DisplayStatus) {
  TestObserver observer;
  watcher_.AddObserver(&observer);
  watcher_.Init(&udev_);
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());

  // Disconnected if there's no status file.
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  NotifyAboutUdevEvent();
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());

  // Disconnected if the status file doesn't report the connected state.
  const char kDisconnected[] = "disconnected";
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(base::WriteFile(status_path, kDisconnected));
  NotifyAboutUdevEvent();
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());

  // Observers should be notified when the device's status goes to "unknown".
  ASSERT_TRUE(base::WriteFile(status_path, DisplayWatcher::kDrmStatusUnknown));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(system::DisplayInfo::ConnectorStatus::UNKNOWN,
            watcher_.GetDisplays().front().connector_status);
  EXPECT_TRUE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(1, observer.num_display_changes());

  // Observers should be notified when the device's status goes to
  // "connected" from "unknown".
  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(system::DisplayInfo::ConnectorStatus::CONNECTED,
            watcher_.GetDisplays().front().connector_status);
  // Make sure observers receive a notification when the status changes from
  // "unknown" to "connected".
  EXPECT_TRUE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(2, observer.num_display_changes());

  // A trailing newline should be okay.
  std::string kConnectedNewline(DisplayWatcher::kDrmStatusConnected);
  kConnectedNewline += "\n";
  ASSERT_TRUE(base::WriteFile(status_path, kConnectedNewline));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(drm_dir_.GetPath().Append(device_path.BaseName()).value(),
            watcher_.GetDisplays()[0].drm_path.value());

  // Add a second disconnected device.
  base::FilePath second_device_path = CreateDrmDevice("card0-DP-0");
  base::FilePath second_status_path =
      second_device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(base::WriteFile(second_status_path, kDisconnected));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(drm_dir_.GetPath().Append(device_path.BaseName()).value(),
            watcher_.GetDisplays()[0].drm_path.value());

  // Connect the second device. It should be reported first since devices are
  // sorted alphabetically.
  ASSERT_TRUE(
      base::WriteFile(second_status_path, DisplayWatcher::kDrmStatusConnected));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(2), watcher_.GetDisplays().size());
  EXPECT_EQ(drm_dir_.GetPath().Append(second_device_path.BaseName()).value(),
            watcher_.GetDisplays()[0].drm_path.value());
  EXPECT_EQ(drm_dir_.GetPath().Append(device_path.BaseName()).value(),
            watcher_.GetDisplays()[1].drm_path.value());

  // Disconnect both devices and create a new device that has a
  // "connected" status but doesn't match the expected naming pattern for a
  // video card.
  ASSERT_TRUE(base::WriteFile(status_path, kDisconnected));
  ASSERT_TRUE(base::WriteFile(second_status_path, kDisconnected));
  base::FilePath misnamed_device_path = CreateDrmDevice("control32");
  base::FilePath misnamed_status_path =
      misnamed_device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(base::WriteFile(misnamed_status_path, kConnectedNewline));
  NotifyAboutUdevEvent();
  EXPECT_EQ(static_cast<size_t>(0), watcher_.GetDisplays().size());
}

TEST_F(DisplayWatcherTest, I2CDevices) {
  // Create a single connected device with no I2C device.
  base::FilePath gpu_device_path = CreateDrmDevice("device");
  base::FilePath card_path = CreateDrmDevice("card0");
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  SetDeviceParent(card_path, gpu_device_path);
  SetDeviceParent(device_path, card_path);
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));

  watcher_.Init(&udev_);
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());

  // Create an I2C device parented to the underlying device, but with a
  // non-MST name, checking that it isn't returned.
  const char kTopLevelI2CName[] = "i2c-2";
  base::FilePath i2c_path = CreateI2CDevice(kTopLevelI2CName);
  auto drm_i2c_path = gpu_device_path.Append(kTopLevelI2CName);
  ASSERT_TRUE(base::CreateDirectory(drm_i2c_path));
  ASSERT_TRUE(base::WriteFile(drm_i2c_path.Append("name"), "DDI B\n"));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());

  // Update its name to DPMST, checking that it is returned.
  ASSERT_TRUE(base::WriteFile(drm_i2c_path.Append("name"), "DPMST\n"));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // Add an extra I2C device under the underlying device that sorts ahead of the
  // previous I2C device, with a non-MST name, to be ignored.
  const char kExtraTopLevelI2CName[] = "i2c-1";
  auto extra_i2c_path = CreateI2CDevice(kExtraTopLevelI2CName);
  auto drm_extra_i2c_path = gpu_device_path.Append(kExtraTopLevelI2CName);
  ASSERT_TRUE(base::CreateDirectory(drm_extra_i2c_path));
  ASSERT_TRUE(base::WriteFile(drm_extra_i2c_path.Append("name"), "DDI A\n"));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // Change the new device to be named DPMST and expect it to be returned,
  // sorting ahead of i2c-2.
  ASSERT_TRUE(base::WriteFile(drm_extra_i2c_path.Append("name"), "DPMST\n"));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(extra_i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // If the I2C device doesn't actually exist, the path shouldn't be set.
  ASSERT_TRUE(brillo::DeleteFile(extra_i2c_path));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // Create an I2C directory within the DRM directory and check that the I2C
  // device's path is set to that device, ignoring any DPMST I2C devices.
  const char kI2CName[] = "i2c-3";
  i2c_path = CreateI2CDevice(kI2CName);
  drm_i2c_path = device_path.Append(kI2CName);
  ASSERT_TRUE(base::CreateDirectory(drm_i2c_path));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // Verify again with no DPMST I2C devices.
  ASSERT_TRUE(
      brillo::DeletePathRecursively(gpu_device_path.Append(kTopLevelI2CName)));
  ASSERT_TRUE(brillo::DeletePathRecursively(
      gpu_device_path.Append(kExtraTopLevelI2CName)));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ(i2c_path.value(), watcher_.GetDisplays()[0].i2c_path.value());

  // If the I2C device doesn't actually exist, the path shouldn't be set.
  ASSERT_TRUE(base::DeleteFile(i2c_path));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());

  // Create a device with a bogus name and check that it doesn't get returned.
  const char kBogusName[] = "i3c-1";
  base::FilePath bogus_path = CreateI2CDevice(kBogusName);
  ASSERT_TRUE(base::CreateDirectory(device_path.Append(kBogusName)));
  ASSERT_TRUE(base::DeleteFile(drm_i2c_path));
  NotifyAboutUdevEvent();
  ASSERT_EQ(static_cast<size_t>(1), watcher_.GetDisplays().size());
  EXPECT_EQ("", watcher_.GetDisplays()[0].i2c_path.value());
}

TEST_F(DisplayWatcherTest, Observer) {
  // The observer shouldn't be notified when Init() is called without any
  // displays present.
  TestObserver observer;
  watcher_.AddObserver(&observer);
  watcher_.Init(&udev_);
  EXPECT_FALSE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(0, observer.num_display_changes());

  // It also shouldn't be notified in response to a Udev event if nothing
  // changed.
  NotifyAboutUdevEvent();
  EXPECT_FALSE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(0, observer.num_display_changes());

  // After adding a display, the observer should be notified.
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));
  NotifyAboutUdevEvent();
  EXPECT_TRUE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(1, observer.num_display_changes());

  // It shouldn't be notified for another no-op Udev event.
  NotifyAboutUdevEvent();
  EXPECT_FALSE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(1, observer.num_display_changes());

  // After the device is disconnected, the observer should be notified one more
  // time.
  ASSERT_TRUE(base::DeleteFile(status_path));
  NotifyAboutUdevEvent();
  EXPECT_TRUE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(2, observer.num_display_changes());

  watcher_.RemoveObserver(&observer);
}

TEST_F(DisplayWatcherTest, DebounceTimer) {
  TestObserver observer;
  watcher_.AddObserver(&observer);
  watcher_.Init(&udev_);

  // After adding a display, the observer should be not be notified before
  // debounce timer expires.
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);
  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));
  NotifyAboutUdevEvent();
  EXPECT_EQ(0, observer.num_display_changes());
  // But on timer expiry, observer should be notified.
  EXPECT_TRUE(watcher_.trigger_debounce_timeout_for_testing());
  EXPECT_EQ(1, observer.num_display_changes());

  watcher_.RemoveObserver(&observer);
}

TEST_F(DisplayWatcherTest, EvdiDeviceSysPath) {
  watcher_.Init(&udev_);
  EXPECT_EQ(0, watcher_.GetDisplays().size());

  // usb -> evdi -> card0 -> card0-DP-1
  base::FilePath usb_path = CreateDevice("usb");
  base::FilePath evdi_path = CreateDevice("evdi");
  base::FilePath card_path = CreateDrmDevice("card0");
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);

  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));
  SetDeviceParent(device_path, card_path);
  SetDeviceParent(card_path, evdi_path);
  SetDeviceParent(evdi_path, usb_path);

  NotifyAboutUdevEvent();
  ASSERT_EQ(1, watcher_.GetDisplays().size());
  // For evdi devices we should return the evdi device's parent as the syspath.
  EXPECT_EQ(usb_path.value(), watcher_.GetDisplays()[0].sys_path.value());
}

TEST_F(DisplayWatcherTest, EvdiDeviceWithoutParentSysPath) {
  watcher_.Init(&udev_);
  EXPECT_EQ(0, watcher_.GetDisplays().size());

  // evdi -> card0 -> card0-DP-1
  base::FilePath evdi_path = CreateDevice("evdi");
  base::FilePath card_path = CreateDrmDevice("card0");
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);

  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));
  SetDeviceParent(device_path, card_path);
  SetDeviceParent(card_path, evdi_path);

  NotifyAboutUdevEvent();
  ASSERT_EQ(1, watcher_.GetDisplays().size());
  // If the evdi device doesn't have a parent, then use the evdi device's
  // syspath.
  EXPECT_EQ(evdi_path.value(), watcher_.GetDisplays()[0].sys_path.value());
}

TEST_F(DisplayWatcherTest, NonEvdiDeviceSysPath) {
  watcher_.Init(&udev_);
  EXPECT_EQ(0, watcher_.GetDisplays().size());

  // usb -> pci -> card0 -> card0-DP-1
  base::FilePath usb_path = CreateDevice("usb");
  base::FilePath pci_path = CreateDevice("pci");
  base::FilePath card_path = CreateDrmDevice("card0");
  base::FilePath device_path = CreateDrmDevice("card0-DP-1");
  base::FilePath status_path =
      device_path.Append(DisplayWatcher::kDrmStatusFile);

  ASSERT_TRUE(
      base::WriteFile(status_path, DisplayWatcher::kDrmStatusConnected));
  SetDeviceParent(device_path, card_path);
  SetDeviceParent(card_path, pci_path);
  SetDeviceParent(pci_path, usb_path);

  NotifyAboutUdevEvent();
  ASSERT_EQ(1, watcher_.GetDisplays().size());
  // If it's not an evdi device, use the syspath of the card's parent device.
  EXPECT_EQ(pci_path.value(), watcher_.GetDisplays()[0].sys_path.value());
}

}  // namespace power_manager::system
