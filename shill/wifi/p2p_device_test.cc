// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/p2p_device.h"

#include <memory>
#include <string>
#include <utility>

#include <base/test/mock_callback.h>

#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/test_event_dispatcher.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/mock_p2p_service.h"
#include "shill/wifi/mock_wifi_phy.h"
#include "shill/wifi/mock_wifi_provider.h"
#include "shill/wifi/wifi_security.h"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Test;

namespace shill {

namespace {
const char kPrimaryInterfaceName[] = "wlan0";
const uint32_t kPhyIndex = 5678;
const uint32_t kSHillID = 0;
const char kP2PSSID[] = "chromeOS-1234";
const char kP2PPassphrase[] = "test0000";
const uint32_t kP2PFrequency = 2437;

}  // namespace

class P2PDeviceTest : public testing::Test {
 public:
  P2PDeviceTest()
      : manager_(&control_interface_, &dispatcher_, &metrics_),
        wifi_provider_(new NiceMock<MockWiFiProvider>(&manager_)),
        wifi_phy_(kPhyIndex) {
    // Replace the Manager's WiFi provider with a mock.
    manager_.wifi_provider_.reset(wifi_provider_);
    // Update the Manager's map from technology to provider.
    manager_.UpdateProviderMapping();
    ON_CALL(*wifi_provider_, GetPhyAtIndex(kPhyIndex))
        .WillByDefault(Return(&wifi_phy_));
  }

 protected:
  StrictMock<base::MockRepeatingCallback<void(LocalDevice::DeviceEvent,
                                              const LocalDevice*)>>
      cb;

  NiceMock<MockControl> control_interface_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  MockWiFiProvider* wifi_provider_;

  MockWiFiPhy wifi_phy_;
};

TEST_F(P2PDeviceTest, DeviceOnOff) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kSHillID, cb.Get());
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
  device->Start();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kReady);
  device->Stop();
  CHECK_EQ(device->state_, P2PDevice::P2PDeviceState::kUninitialized);
}

TEST_F(P2PDeviceTest, ConnectAndDisconnect) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kSHillID, cb.Get());
  EXPECT_TRUE(device->Start());

  // Initiate connection with a new service.
  auto service0 = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_TRUE(device->Connect(std::move(service0)));

  // Attampting to connect again should be a no-op and and return false.
  auto service1 = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_FALSE(device->Connect(std::move(service1)));

  // Disconnect.
  EXPECT_TRUE(device->Disconnect());
}

TEST_F(P2PDeviceTest, BadState_Client) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PClient,
                    kPrimaryInterfaceName, kPhyIndex, kSHillID, cb.Get());
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_FALSE(device->Connect(std::move(service)));
  EXPECT_FALSE(device->Disconnect());
  EXPECT_TRUE(device->Start());
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_TRUE(device->Connect(std::move(service)));
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_FALSE(device->Connect(std::move(service)));
  EXPECT_TRUE(device->Disconnect());
}

TEST_F(P2PDeviceTest, BadState_GO) {
  scoped_refptr<P2PDevice> device =
      new P2PDevice(&manager_, LocalDevice::IfaceType::kP2PGO,
                    kPrimaryInterfaceName, kPhyIndex, kSHillID, cb.Get());
  auto service = std::make_unique<MockP2PService>(
      device, kP2PSSID, kP2PPassphrase, kP2PFrequency);
  EXPECT_FALSE(device->CreateGroup(std::move(service)));
  EXPECT_FALSE(device->RemoveGroup());
  EXPECT_TRUE(device->Start());
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_TRUE(device->CreateGroup(std::move(service)));
  service = std::make_unique<MockP2PService>(device, kP2PSSID, kP2PPassphrase,
                                             kP2PFrequency);
  EXPECT_FALSE(device->CreateGroup(std::move(service)));
  EXPECT_TRUE(device->RemoveGroup());
}

}  // namespace shill
