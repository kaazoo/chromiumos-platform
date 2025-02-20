// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/real_shill_provider.h"

#include <memory>
#include <tuple>
#include <utility>

#include <base/memory/ptr_util.h>
#include <base/time/time.h>
#include <brillo/message_loops/fake_message_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <shill/dbus-constants.h>
#include <shill/dbus-proxies.h>
#include <shill/dbus-proxy-mocks.h>

#include "update_engine/common/test_utils.h"
#include "update_engine/cros/dbus_test_utils.h"
#include "update_engine/cros/fake_shill_proxy.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/update_manager/umtest_utils.h"

using base::Time;
using chromeos_update_engine::ConnectionType;
using chromeos_update_engine::FakeSystemState;
using org::chromium::flimflam::ManagerProxyMock;
using org::chromium::flimflam::ServiceProxyMock;
using std::unique_ptr;
using testing::_;
using testing::DoAll;
using testing::Mock;
using testing::Return;
using testing::SetArgPointee;

namespace {

// Fake service paths.
const char* const kFakeEthernetServicePath = "/fake/ethernet/service";
const char* const kFakeWifiServicePath = "/fake/wifi/service";
const char* const kFakeCellularServicePath = "/fake/cellular/service";
const char* const kFakeVpnServicePath = "/fake/vpn/service";
const char* const kFakeUnknownServicePath = "/fake/unknown/service";

const bool kMetered = true;
const bool kUnmetered = false;

}  // namespace

namespace chromeos_update_manager {

class UmRealShillProviderTest : public ::testing::Test {
 protected:
  // Initialize the RealShillProvider under test.
  void SetUp() override {
    FakeSystemState::CreateInstance();
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(InitTime());
    loop_.SetAsCurrent();
    fake_shill_proxy_ = new chromeos_update_engine::FakeShillProxy();
    provider_.reset(new RealShillProvider(fake_shill_proxy_));

    ManagerProxyMock* manager_proxy_mock = fake_shill_proxy_->GetManagerProxy();

    // The PropertyChanged signal should be subscribed to.
    MOCK_SIGNAL_HANDLER_EXPECT_SIGNAL_HANDLER(
        manager_property_changed_, *manager_proxy_mock, PropertyChanged);
  }

  void TearDown() override {
    provider_.reset();
    // Check for leaked callbacks on the main loop.
    EXPECT_FALSE(loop_.PendingTasks());
  }

  // These methods generate fixed timestamps for use in faking the current time.
  Time InitTime() {
    Time::Exploded now_exp;
    now_exp.year = 2014;
    now_exp.month = 3;
    now_exp.day_of_week = 2;
    now_exp.day_of_month = 18;
    now_exp.hour = 8;
    now_exp.minute = 5;
    now_exp.second = 33;
    now_exp.millisecond = 675;
    Time time;
    std::ignore = Time::FromLocalExploded(now_exp, &time);
    return time;
  }

  Time ConnChangedTime() { return InitTime() + base::Seconds(10); }

  // Sets the default_service object path in the response from the
  // ManagerProxyMock instance.
  void SetManagerReply(const char* default_service, bool reply_succeeds);

  // Sets the `service_type`, `physical_technology` and `service_metered`
  // properties in the mocked service `service_path`. If any of the pointers is
  // a nullptr, the corresponding property will not be included in the response.
  // Returns the mock object pointer, owned by the `fake_shill_proxy_`.
  ServiceProxyMock* SetServiceReply(const std::string& service_path,
                                    const char* service_type,
                                    const char* physical_technology,
                                    const bool* service_metered);

  void InitWithDefaultService(const char* default_service) {
    SetManagerReply(default_service, true);
    // Check that provider initializes correctly.
    EXPECT_TRUE(provider_->Init());
    // RunOnce to notify the signal handler was connected properly.
    EXPECT_TRUE(loop_.RunOnce(false));
  }

  // Sends a signal informing the provider about a default connection
  // `service_path`. Sets the fake connection change time in
  // `conn_change_time_p` if provided.
  void SendDefaultServiceSignal(const std::string& service_path,
                                Time* conn_change_time_p) {
    const Time conn_change_time = ConnChangedTime();
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(conn_change_time);
    ASSERT_TRUE(manager_property_changed_.IsHandlerRegistered());
    manager_property_changed_.signal_callback().Run(
        shill::kDefaultServiceProperty, dbus::ObjectPath(service_path));
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(conn_change_time +
                                                           base::Seconds(5));
    if (conn_change_time_p) {
      *conn_change_time_p = conn_change_time;
    }
  }

  // Sets up expectations for detection of a connection `service_path` with type
  // `shill_type_str`. Ensures that the new connection status and change time
  // are properly detected by the provider. Writes the fake connection change
  // time to `conn_change_time_p`, if provided.
  void SetupConnectionAndAttrs(const std::string& service_path,
                               const char* shill_type,
                               const bool* shill_metered,
                               Time* conn_change_time_p) {
    SetServiceReply(service_path, shill_type, nullptr, shill_metered);
    // Note: We don't setup this `service_path` as the default service path but
    // we instead send a signal notifying the change since the code won't call
    // GetProperties on the Manager object at this point.

    // Send a signal about a new default service.
    Time conn_change_time;
    SendDefaultServiceSignal(service_path, &conn_change_time);

    // Query the connection status, ensure last change time reported correctly.
    UmTestUtils::ExpectVariableHasValue(true, provider_->var_is_connected());
    UmTestUtils::ExpectVariableHasValue(conn_change_time,
                                        provider_->var_conn_last_changed());

    // Write the connection change time to the output argument.
    if (conn_change_time_p) {
      *conn_change_time_p = conn_change_time;
    }
  }

  // Sets up a connection and tests that its type is being properly detected by
  // the provider.
  void SetupConnectionAndTestType(const char* service_path,
                                  const char* shill_type,
                                  ConnectionType expected_conn_type) {
    // Set up and test the connection, record the change time.
    Time conn_change_time;
    SetupConnectionAndAttrs(service_path, shill_type, nullptr,
                            &conn_change_time);

    // Query the connection type, ensure last change time did not change.
    UmTestUtils::ExpectVariableHasValue(expected_conn_type,
                                        provider_->var_conn_type());
    UmTestUtils::ExpectVariableHasValue(conn_change_time,
                                        provider_->var_conn_last_changed());
  }

  // Sets up a connection and tests that its metered property is being properly
  // detected by the provider.
  void SetupConnectionAndTestMetered(const char* service_path,
                                     const bool* shill_metered,
                                     bool expected_metered) {
    // Set up and test the connection, record the change time.
    Time conn_change_time;
    SetupConnectionAndAttrs(service_path, shill::kTypeWifi, shill_metered,
                            &conn_change_time);

    // Query the metered property, ensure last change time did not change.
    UmTestUtils::ExpectVariableHasValue(expected_metered,
                                        provider_->var_is_metered());
    UmTestUtils::ExpectVariableHasValue(conn_change_time,
                                        provider_->var_conn_last_changed());
  }

  brillo::FakeMessageLoop loop_{nullptr};
  chromeos_update_engine::FakeShillProxy* fake_shill_proxy_;

  // The registered signal handler for the signal Manager.PropertyChanged.
  chromeos_update_engine::dbus_test_utils::MockSignalHandler<void(
      const std::string&, const brillo::Any&)>
      manager_property_changed_;

  unique_ptr<RealShillProvider> provider_;
};

void UmRealShillProviderTest::SetManagerReply(const char* default_service,
                                              bool reply_succeeds) {
  ManagerProxyMock* manager_proxy_mock = fake_shill_proxy_->GetManagerProxy();
  if (!reply_succeeds) {
    EXPECT_CALL(*manager_proxy_mock, GetProperties(_, _, _))
        .WillOnce(Return(false));
    return;
  }

  // Create a dictionary of properties and optionally include the default
  // service.
  brillo::VariantDictionary reply_dict;
  reply_dict["SomeOtherProperty"] = 0xC0FFEE;

  if (default_service) {
    reply_dict[shill::kDefaultServiceProperty] =
        dbus::ObjectPath(default_service);
  }
  EXPECT_CALL(*manager_proxy_mock, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(reply_dict), Return(true)));
}

ServiceProxyMock* UmRealShillProviderTest::SetServiceReply(
    const std::string& service_path,
    const char* service_type,
    const char* physical_technology,
    const bool* service_metered) {
  brillo::VariantDictionary reply_dict;
  reply_dict["SomeOtherProperty"] = 0xC0FFEE;

  if (service_type) {
    reply_dict[shill::kTypeProperty] = std::string(service_type);
  }

  if (physical_technology) {
    reply_dict[shill::kPhysicalTechnologyProperty] =
        std::string(physical_technology);
  }

  if (service_metered) {
    reply_dict[shill::kMeteredProperty] = *service_metered;
  }

  ServiceProxyMock* service_proxy_mock = new ServiceProxyMock();

  // Plumb return value into mock object.
  EXPECT_CALL(*service_proxy_mock, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(reply_dict), Return(true)));

  fake_shill_proxy_->SetServiceForPath(dbus::ObjectPath(service_path),
                                       base::WrapUnique(service_proxy_mock));

  return service_proxy_mock;
}

// Query the connection status, type and time last changed, as they were set
// during initialization (no signals).
TEST_F(UmRealShillProviderTest, ReadBaseValues) {
  InitWithDefaultService("/");
  // Query the provider variables.
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_is_connected());
  UmTestUtils::ExpectVariableNotSet(provider_->var_conn_type());
  UmTestUtils::ExpectVariableHasValue(InitTime(),
                                      provider_->var_conn_last_changed());
}

// Ensure that invalid DBus paths are ignored.
TEST_F(UmRealShillProviderTest, InvalidServicePath) {
  InitWithDefaultService("invalid");
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_is_connected());
  UmTestUtils::ExpectVariableNotSet(provider_->var_conn_type());
  UmTestUtils::ExpectVariableHasValue(InitTime(),
                                      provider_->var_conn_last_changed());
}

// Ensure that a service path property including a different type is ignored.
TEST_F(UmRealShillProviderTest, InvalidServicePathType) {
  ManagerProxyMock* manager_proxy_mock = fake_shill_proxy_->GetManagerProxy();
  brillo::VariantDictionary reply_dict;
  reply_dict[shill::kDefaultServiceProperty] = "/not/an/object/path";
  EXPECT_CALL(*manager_proxy_mock, GetProperties(_, _, _))
      .WillOnce(DoAll(SetArgPointee<0>(reply_dict), Return(true)));

  EXPECT_TRUE(provider_->Init());
  EXPECT_TRUE(loop_.RunOnce(false));

  UmTestUtils::ExpectVariableHasValue(false, provider_->var_is_connected());
}

// Test that Ethernet connection is identified correctly.
TEST_F(UmRealShillProviderTest, ReadConnTypeEthernet) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeEthernetServicePath, shill::kTypeEthernet,
                             ConnectionType::kEthernet);
}

// Test that Wifi connection is identified correctly.
TEST_F(UmRealShillProviderTest, ReadConnTypeWifi) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeWifiServicePath, shill::kTypeWifi,
                             ConnectionType::kWifi);
}

// Test that Cellular connection is identified correctly.
TEST_F(UmRealShillProviderTest, ReadConnTypeCellular) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeCellularServicePath, shill::kTypeCellular,
                             ConnectionType::kCellular);
}

// Test that an unknown connection is identified as such.
TEST_F(UmRealShillProviderTest, ReadConnTypeUnknown) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeUnknownServicePath, "FooConnectionType",
                             ConnectionType::kUnknown);
}

// Tests that VPN connection is identified correctly.
TEST_F(UmRealShillProviderTest, ReadConnTypeVpn) {
  InitWithDefaultService("/");
  // Mock logic for returning a default service path and its type.
  SetServiceReply(kFakeVpnServicePath, shill::kTypeVPN, shill::kTypeWifi,
                  nullptr);

  // Send a signal about a new default service.
  Time conn_change_time;
  SendDefaultServiceSignal(kFakeVpnServicePath, &conn_change_time);

  // Query the connection type, ensure last change time reported correctly.
  UmTestUtils::ExpectVariableHasValue(ConnectionType::kWifi,
                                      provider_->var_conn_type());
  UmTestUtils::ExpectVariableHasValue(conn_change_time,
                                      provider_->var_conn_last_changed());
}

// Ensure that the connection type is properly cached in the provider through
// subsequent variable readings.
TEST_F(UmRealShillProviderTest, ConnTypeCacheUsed) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeEthernetServicePath, shill::kTypeEthernet,
                             ConnectionType::kEthernet);

  UmTestUtils::ExpectVariableHasValue(ConnectionType::kEthernet,
                                      provider_->var_conn_type());
}

// Ensure that the cached connection type remains valid even when a default
// connection signal occurs but the connection is not changed.
TEST_F(UmRealShillProviderTest, ConnTypeCacheRemainsValid) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeEthernetServicePath, shill::kTypeEthernet,
                             ConnectionType::kEthernet);

  SendDefaultServiceSignal(kFakeEthernetServicePath, nullptr);

  UmTestUtils::ExpectVariableHasValue(ConnectionType::kEthernet,
                                      provider_->var_conn_type());
}

// Ensure that the cached connection type is invalidated and re-read when the
// default connection changes.
TEST_F(UmRealShillProviderTest, ConnTypeCacheInvalidated) {
  InitWithDefaultService("/");
  SetupConnectionAndTestType(kFakeEthernetServicePath, shill::kTypeEthernet,
                             ConnectionType::kEthernet);

  SetupConnectionAndTestType(kFakeWifiServicePath, shill::kTypeWifi,
                             ConnectionType::kWifi);
}

// Test that an unmetered connection is identified correctly.
TEST_F(UmRealShillProviderTest, ReadConnUnmetered) {
  InitWithDefaultService("/");
  SetupConnectionAndTestMetered(kFakeWifiServicePath,
                                /*shill_metered=*/&kUnmetered,
                                /*expected_metered=*/kUnmetered);
}

// Test that a metered connection is identified correctly.
TEST_F(UmRealShillProviderTest, ReadConnMetered) {
  InitWithDefaultService("/");
  SetupConnectionAndTestMetered(kFakeWifiServicePath,
                                /*shill_metered=*/&kMetered,
                                /*expected_metered=*/kMetered);
}

// Test that a metered property unknown connection is correctly identified as
// unmetered.
TEST_F(UmRealShillProviderTest, ReadConnMeteredUnknown) {
  InitWithDefaultService("/");
  SetupConnectionAndTestMetered(kFakeWifiServicePath,
                                /*shill_metered=*/nullptr,
                                /*expected_metered=*/kUnmetered);
}

// Ensure that the connection metered property is properly cached in the
// provider.
TEST_F(UmRealShillProviderTest, ConnMeteredCacheUsed) {
  InitWithDefaultService("/");
  SetupConnectionAndTestMetered(kFakeEthernetServicePath,
                                /*shill_metered=*/nullptr,
                                /*expected_metered=*/kUnmetered);

  UmTestUtils::ExpectVariableHasValue(kUnmetered, provider_->var_is_metered());
}

// Ensure that the cached connection metered property remains valid even when a
// default connection signal occurs but the connection is not changed.
TEST_F(UmRealShillProviderTest, ConnMeteredCacheRemainsValid) {
  InitWithDefaultService("/");
  SetupConnectionAndTestMetered(kFakeEthernetServicePath,
                                /*shill_metered=*/nullptr,
                                /*expected_metered=*/kUnmetered);

  SendDefaultServiceSignal(kFakeEthernetServicePath, nullptr);

  UmTestUtils::ExpectVariableHasValue(kUnmetered, provider_->var_is_metered());
}

// Ensure that the cached connection metered property is invalidated and re-read
// when the default connection changes.
TEST_F(UmRealShillProviderTest, ConnMeteredCacheInvalidated) {
  InitWithDefaultService("/");
  SetupConnectionAndTestMetered(kFakeEthernetServicePath,
                                /*shill_metered=*/nullptr,
                                /*expected_metered=*/kUnmetered);

  SetupConnectionAndTestMetered(kFakeWifiServicePath,
                                /*shill_metered=*/&kMetered,
                                /*expected_metered=*/kMetered);
}

// Fake two DBus signals prompting a default connection change, but otherwise
// give the same service path. Check connection status and the time it was last
// changed, making sure that it is the time when the first signal was sent (and
// not the second).
TEST_F(UmRealShillProviderTest, ReadLastChangedTimeTwoSignals) {
  InitWithDefaultService("/");
  // Send a default service signal twice, advancing the clock in between.
  Time conn_change_time;
  SetupConnectionAndAttrs(kFakeEthernetServicePath, shill::kTypeEthernet,
                          nullptr, &conn_change_time);
  // This will set the service path to the same value, so it should not call
  // GetProperties() again.
  SendDefaultServiceSignal(kFakeEthernetServicePath, nullptr);

  // Query the connection status, ensure last change time reported as the first
  // time the signal was sent.
  UmTestUtils::ExpectVariableHasValue(true, provider_->var_is_connected());
  UmTestUtils::ExpectVariableHasValue(conn_change_time,
                                      provider_->var_conn_last_changed());
}

// Make sure that the provider initializes correctly even if shill is not
// responding, that variables can be obtained, and that they all return a null
// value (indicating that the underlying values were not set).
TEST_F(UmRealShillProviderTest, NoInitConnStatusReadBaseValues) {
  // Initialize the provider, no initial connection status response.
  SetManagerReply(nullptr, false);
  EXPECT_TRUE(provider_->Init());
  EXPECT_TRUE(loop_.RunOnce(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_is_connected());
  UmTestUtils::ExpectVariableNotSet(provider_->var_conn_type());
  UmTestUtils::ExpectVariableNotSet(provider_->var_conn_last_changed());
}

// Test that, once a signal is received, the connection status and other info
// can be read correctly.
TEST_F(UmRealShillProviderTest, NoInitConnStatusReadConnTypeEthernet) {
  // Initialize the provider with no initial connection status response.
  SetManagerReply(nullptr, false);
  EXPECT_TRUE(provider_->Init());
  EXPECT_TRUE(loop_.RunOnce(false));

  SetupConnectionAndAttrs(kFakeEthernetServicePath, shill::kTypeEthernet,
                          nullptr, nullptr);
  UmTestUtils::ExpectVariableHasValue(true, provider_->var_is_connected());
}

}  // namespace chromeos_update_manager
