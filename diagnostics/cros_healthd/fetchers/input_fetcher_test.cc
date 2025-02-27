// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/input_fetcher.h"

#include <optional>
#include <utility>
#include <vector>

#include <base/test/gmock_callback_support.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/cros_healthd/executor/mock_executor.h"
#include "diagnostics/cros_healthd/system/fake_mojo_service.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/mojo_task_environment.h"
#include "diagnostics/mojom/external/cros_healthd_internal.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {
namespace {

namespace internal_mojom = ::ash::cros_healthd::internal::mojom;
namespace mojom = ::ash::cros_healthd::mojom;

using ::testing::_;

class InputFetcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mock_context_.fake_mojo_service()->InitializeFakeMojoService();

    ON_CALL(*mock_executor(), GetTouchpadDevices(_))
        .WillByDefault(base::test::RunOnceCallback<0>(
            std::vector<mojom::TouchpadDevicePtr>{}, std::nullopt));
  }

  mojom::InputResultPtr FetchInputInfoSync() {
    base::test::TestFuture<mojom::InputResultPtr> future;
    FetchInputInfo(&mock_context_, future.GetCallback());
    return future.Take();
  }

  FakeChromiumDataCollector& fake_chromium_data_collector() {
    return mock_context_.fake_mojo_service()->fake_chromium_data_collector();
  }

  MockExecutor* mock_executor() { return mock_context_.mock_executor(); }

  MojoTaskEnvironment env_;
  MockContext mock_context_;
};

TEST_F(InputFetcherTest, FetchTouchscreenDevices) {
  auto fake_device = internal_mojom::TouchscreenDevice::New();
  fake_device->input_device = internal_mojom::InputDevice::New();
  fake_device->input_device->name = "FakeName";
  fake_device->input_device->connection_type =
      internal_mojom::InputDevice::ConnectionType::kBluetooth;
  fake_device->input_device->physical_location = "physical_location";
  fake_device->input_device->is_enabled = true;
  fake_device->input_device->sysfs_path = "sysfs_path";
  fake_device->touch_points = 42;
  fake_device->has_stylus = true;
  fake_device->has_stylus_garage_switch = true;
  fake_chromium_data_collector().touchscreen_devices().push_back(
      fake_device.Clone());

  auto expected_device = mojom::TouchscreenDevice::New();
  expected_device->input_device = mojom::InputDevice::New();
  expected_device->input_device->name = "FakeName";
  expected_device->input_device->connection_type =
      mojom::InputDevice::ConnectionType::kBluetooth;
  expected_device->input_device->physical_location = "physical_location";
  expected_device->input_device->is_enabled = true;
  expected_device->touch_points = 42;
  expected_device->has_stylus = true;
  expected_device->has_stylus_garage_switch = true;

  auto result = FetchInputInfoSync();
  ASSERT_TRUE(result->is_input_info());
  ASSERT_EQ(result->get_input_info()->touchscreen_devices.size(), 1);
  EXPECT_EQ(result->get_input_info()->touchscreen_devices[0], expected_device);
}

TEST_F(InputFetcherTest, FetchTouchpadLibraryName) {
  fake_chromium_data_collector().touchpad_library_name() =
      "FakeTouchpadLibraryName";

  auto result = FetchInputInfoSync();
  ASSERT_TRUE(result->is_input_info());
  EXPECT_EQ(result->get_input_info()->touchpad_library_name,
            "FakeTouchpadLibraryName");
}

TEST_F(InputFetcherTest, FetchTouchpadDevices) {
  auto fake_device = mojom::TouchpadDevice::New();
  auto input_device = mojom::InputDevice::New();
  input_device->connection_type = mojom::InputDevice::ConnectionType::kInternal;
  input_device->physical_location = "physical_location";
  input_device->is_enabled = true;
  input_device->name = "FakeName";

  fake_device->input_device = std::move(input_device);
  fake_device->driver_name = "FakeDriver";

  std::vector<mojom::TouchpadDevicePtr> expected_result;
  expected_result.push_back(fake_device->Clone());
  EXPECT_CALL(*mock_executor(), GetTouchpadDevices(_))
      .WillOnce(base::test::RunOnceCallback<0>(std::move(expected_result),
                                               std::nullopt));

  auto result = FetchInputInfoSync();
  ASSERT_TRUE(result->is_input_info());
  const auto& touchpad_devices = result->get_input_info()->touchpad_devices;
  ASSERT_TRUE(touchpad_devices.has_value());
  ASSERT_EQ(touchpad_devices->size(), 1);
  EXPECT_EQ(touchpad_devices.value()[0], fake_device);
}

TEST_F(InputFetcherTest, FetchTouchpadDevicesHasError) {
  EXPECT_CALL(*mock_executor(), GetTouchpadDevices(_))
      .WillOnce(base::test::RunOnceCallback<0>(
          std::vector<mojom::TouchpadDevicePtr>{}, "An error has occurred"));

  auto result = FetchInputInfoSync();
  ASSERT_TRUE(result->is_input_info());
  const auto& touchpad_devices = result->get_input_info()->touchpad_devices;
  ASSERT_FALSE(touchpad_devices.has_value());
}

TEST_F(InputFetcherTest, FetchFailed) {
  // Reset the receiver to emulate the service disconnected.
  fake_chromium_data_collector().receiver().reset();

  auto result = FetchInputInfoSync();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error()->type, mojom::ErrorType::kServiceUnavailable);
}

}  // namespace
}  // namespace diagnostics
