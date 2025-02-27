// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service_provider.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/test/mock_callback.h>
#include <chromeos/chromeos-config/libcros_config/fake_cros_config.h>
#include <chromeos/net-base/mac_address.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_capability_3gpp.h"
#include "shill/cellular/mock_cellular.h"
#include "shill/cellular/mock_modem_info.h"
#include "shill/dbus/dbus_properties_proxy.h"
#include "shill/dbus/fake_properties_proxy.h"
#include "shill/mock_control.h"
#include "shill/mock_manager.h"
#include "shill/mock_metrics.h"
#include "shill/mock_profile.h"
#include "shill/network/mock_network.h"
#include "shill/network/network.h"
#include "shill/service.h"
#include "shill/store/fake_store.h"
#include "shill/test_event_dispatcher.h"
#include "shill/tethering_manager.h"

using testing::_;
using testing::Invoke;
using testing::Mock;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;
using testing::WithArgs;

namespace shill {

namespace {

constexpr char kTestDeviceName[] = "usb0";
constexpr net_base::MacAddress kTestDeviceAddress(
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05);
constexpr int kTestInterfaceIndex = 1;
constexpr char kDBusService[] = "org.freedesktop.ModemManager1";
const RpcIdentifier kDBusPath("/org/freedesktop/ModemManager1/Modem/0");
// EID must be 32 chars
constexpr char kEid1[] = "eid1_678901234567890123456789012";
constexpr char kEid2[] = "eid2_678901234567890123456789012";

CellularService* AsCellularService(ServiceRefPtr service) {
  if (service->technology() != Technology::kCellular) {
    return nullptr;
  }
  return static_cast<CellularService*>(service.get());
}

}  // namespace

class CellularServiceProviderTest : public testing::Test {
 public:
  CellularServiceProviderTest()
      : manager_(&control_, &dispatcher_, &metrics_),
        modem_info_(&control_, &manager_) {}

  ~CellularServiceProviderTest() override = default;

  void SetUp() override {
    EXPECT_CALL(manager_, modem_info()).WillRepeatedly(Return(&modem_info_));
    provider_ = std::make_unique<CellularServiceProvider>(&manager_);
    auto fake_cros_config = std::make_unique<brillo::FakeCrosConfig>();
    fake_cros_config_ = fake_cros_config.get();
    provider_->set_cros_config_for_testing(std::move(fake_cros_config));
    provider_->Start();
    profile_ = new NiceMock<MockProfile>(&manager_);
    provider_->set_profile_for_testing(profile_);
    EXPECT_CALL(*profile_, GetConstStorage()).WillRepeatedly(Return(&storage_));
    EXPECT_CALL(*profile_, GetStorage()).WillRepeatedly(Return(&storage_));
    EXPECT_CALL(manager_, cellular_service_provider())
        .WillRepeatedly(Return(provider_.get()));
  }

  void TearDown() override {
    provider_->Stop();
    provider_.reset();
    CHECK(profile_->HasOneRef());
    profile_ = nullptr;
  }

  // TODO(b/154014577): Provide eID for identifying sim cards once supported.
  CellularRefPtr CreateDevice(const std::string& imsi,
                              const std::string& iccid) {
    CellularRefPtr cellular = new Cellular(
        &manager_, kTestDeviceName, kTestDeviceName, kTestDeviceAddress,
        kTestInterfaceIndex, kDBusService, kDBusPath);
    if (!iccid.empty()) {
      Cellular::SimProperties sim_properties;
      sim_properties.iccid = iccid;
      sim_properties.imsi = imsi;
      cellular->SetPrimarySimProperties(sim_properties);
    }
    return cellular;
  }

  CellularRefPtr CreateDeviceWithEid(const std::string& imsi,
                                     const std::string& iccid,
                                     const std::string& eid) {
    CellularRefPtr cellular = CreateDevice(imsi, iccid);
    cellular->set_eid_for_testing(eid);
    return cellular;
  }

  // TODO(b/154014577): Provide eID once supported.
  void SetupCellularStore(const std::string& identifier,
                          const std::string& imsi,
                          const std::string& iccid,
                          const std::string& sim_card_id) {
    storage_.SetString(identifier, kTypeProperty, kTypeCellular);
    storage_.SetString(identifier, CellularService::kStorageImsi, imsi);
    storage_.SetString(identifier, CellularService::kStorageIccid, iccid);
    storage_.SetString(identifier, CellularService::kStorageSimCardId,
                       sim_card_id);
  }

  void StoreCellularProperty(const std::string& identifier,
                             const std::string& key,
                             const std::string& value) {
    storage_.SetString(identifier, key, value);
  }

  std::set<std::string> GetStorageGroups() { return storage_.GetGroups(); }

  void SetVariantThatSupportsTethering() {
    fake_cros_config_->SetString("/modem", "firmware-variant", "crota_fm101");
  }

  void SetVariantThatDoesNotSupportTethering() {
    fake_cros_config_->SetString("/modem", "firmware-variant", "limozeen");
  }

  const std::vector<CellularServiceRefPtr>& GetProviderServices() const {
    return provider_->services_;
  }

  CellularServiceProvider* provider() { return provider_.get(); }
  ProfileRefPtr profile() { return profile_; }

  void DispatchPendingEvents() { dispatcher_.DispatchPendingEvents(); }

 protected:
  brillo::FakeCrosConfig* fake_cros_config_;
  EventDispatcherForTest dispatcher_;
  NiceMock<MockControl> control_;
  NiceMock<MockMetrics> metrics_;
  NiceMock<MockManager> manager_;
  MockModemInfo modem_info_;
  FakeStore storage_;
  scoped_refptr<NiceMock<MockProfile>> profile_;
  std::unique_ptr<CellularServiceProvider> provider_;
};

TEST_F(CellularServiceProviderTest, LoadService) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("", service->eid());
  EXPECT_TRUE(service->IsVisible());
  EXPECT_TRUE(service->connectable());

  // Stopping should remove all services.
  provider()->Stop();
  EXPECT_EQ(0u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, RemoveServices) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ(1u, GetProviderServices().size());

  provider()->RemoveServices();
  EXPECT_EQ(0u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, LoadServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  // Add an entry in the storage with a saved property (ppp_username).
  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user1");

  // Ensure that the service is loaded from storage.
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("user1", service->ppp_username());
}

TEST_F(CellularServiceProviderTest, LoadMultipleServicesFromProfile) {
  // Set up two cellular services with the same SIM Card Id.
  SetupCellularStore("cellular_1a", "imsi1a", "iccid1a", kEid1);
  SetupCellularStore("cellular_1b", "imsi1b", "iccid1b", kEid1);
  // Set up a third cellular service with a different SIM Card Id.
  SetupCellularStore("cellular_2", "imsi2", "iccid2", kEid2);

  CellularRefPtr device = CreateDeviceWithEid("imsi1a", "iccid1a", kEid1);

  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  // cellular_1a should be returned.
  EXPECT_EQ("imsi1a", service->imsi());
  EXPECT_EQ("iccid1a", service->iccid());

  // Only cellular_1a should be created even though cellualr_1b is present on
  // the same EID.
  const std::vector<CellularServiceRefPtr>& provider_services =
      GetProviderServices();
  ASSERT_EQ(1u, provider_services.size());
  CellularServiceRefPtr service1a = provider_services[0];
  EXPECT_EQ("iccid1a", service1a->iccid());
  EXPECT_TRUE(service1a->connectable());
}

// When a SIM is switched (e.g. after a hotswap), LoadServicesForDevice will be
// called with a different primary ICCID. This should create a new Service, and
// destroy the old Service when RemoveNonDeviceServices is called.
TEST_F(CellularServiceProviderTest, SwitchDeviceIccid) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ(1u, GetProviderServices().size());
  unsigned int serial_number1 = service->serial_number();

  // Adding a device with a new ICCID should create a new service with a
  // different serial number.
  Cellular::SimProperties sim_properties;
  sim_properties.iccid = "iccid2";
  sim_properties.imsi = "imsi2";
  std::vector<Cellular::SimProperties> slot_properties;
  slot_properties.push_back(sim_properties);
  device->SetSimProperties(slot_properties, 0u);
  service = provider()->LoadServicesForDevice(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("iccid2", service->iccid());
  provider()->RemoveNonDeviceServices(device.get());
  EXPECT_EQ(1u, GetProviderServices().size());
  EXPECT_NE(serial_number1, service->serial_number());

  // Stopping should remove all services.
  provider()->Stop();
  EXPECT_EQ(0u, GetProviderServices().size());
}

// When the active SIM slot is switched, UpdateServices() should update
// the State and Strength properties of the inactive Service.
TEST_F(CellularServiceProviderTest, SwitchSimSlot) {
  CellularRefPtr cellular = CreateDevice("", "");

  // Set the Cellular State to Enabled so that UpdateServices() behaves as
  // expected. This requires creating a DBusPropertiesProxy for the Capability.
  static_cast<CellularCapability3gpp*>(cellular->capability_for_testing())
      ->SetDBusPropertiesProxyForTesting(
          DBusPropertiesProxy::CreateDBusPropertiesProxyForTesting(
              std::make_unique<FakePropertiesProxy>()));
  cellular->set_state_for_testing(Cellular::State::kEnabled);

  Cellular::SimProperties sim1_properties;
  sim1_properties.iccid = "iccid1";
  sim1_properties.imsi = "imsi1";
  Cellular::SimProperties sim2_properties;
  sim2_properties.eid = kEid1;
  sim2_properties.iccid = "iccid2";
  sim2_properties.imsi = "imsi2";
  std::vector<Cellular::SimProperties> slot_properties;
  slot_properties.push_back(sim1_properties);
  slot_properties.push_back(sim2_properties);
  cellular->SetSimProperties(slot_properties, /*primary=*/0);

  CellularServiceRefPtr service1 =
      provider()->LoadServicesForDevice(cellular.get());
  ASSERT_TRUE(service1);
  EXPECT_EQ("iccid1", service1->iccid());

  // Set the Service to connected with a non 0 signal strength.
  service1->SetConnectable(true);
  service1->SetState(Service::kStateConnected);
  service1->SetStrength(50);

  // Setting the other SIM to primary should clear the |service1| properties
  // associated with being connected.
  cellular->SetSimProperties(slot_properties, /*primary=*/1);
  EXPECT_EQ("iccid2", cellular->iccid());
  CellularServiceRefPtr service2 =
      provider()->LoadServicesForDevice(cellular.get());
  ASSERT_TRUE(service2);
  EXPECT_EQ("iccid2", service2->iccid());

  provider()->UpdateServices(cellular.get());
  // |service1| is still connectable since it is an available SIM.
  EXPECT_TRUE(service1->connectable());
  // |service1| State is set to Idle and Strength is set to 0.
  EXPECT_EQ(Service::kStateIdle, service1->state());
  EXPECT_EQ(0u, service1->strength());

  provider()->Stop();
  cellular->SetServiceForTesting(nullptr);

  service1->SetDevice(nullptr);
  service2->SetDevice(nullptr);
  ASSERT_TRUE(cellular->HasOneRef());
  cellular = nullptr;
}

TEST_F(CellularServiceProviderTest, FindLastOnline) {
  CellularRefPtr device = CreateDeviceWithEid("imsi1", "iccid1", kEid1);
  std::string identifier = device->GetStorageIdentifier();

  SetupCellularStore("cellular1", "imsi1", "iccid1", kEid1);
  SetupCellularStore("cellular2", "imsi2", "iccid2", kEid1);

  // Neither service has been online.
  storage_.SetUint64("cellular1", Service::kStorageStartTime, 1);
  storage_.SetUint64("cellular2", Service::kStorageStartTime, 2);
  provider()->LoadServicesForDevice(device.get());
  provider()->LoadServicesForSecondarySim(kEid1, "iccid2", "imsi2",
                                          device.get());
  ASSERT_EQ(2u, GetProviderServices().size());
  // Return the latest |StartTime|.
  ASSERT_EQ(
      2,
      provider()->FindLastOnline().ToDeltaSinceWindowsEpoch().InMilliseconds());
  provider()->RemoveServices();
  EXPECT_EQ(0u, GetProviderServices().size());

  // Only one service has been online.
  storage_.SetUint64("cellular1", Service::kStorageLastOnline, 11);
  provider()->LoadServicesForDevice(device.get());
  provider()->LoadServicesForSecondarySim(kEid1, "iccid2", "imsi2",
                                          device.get());
  ASSERT_EQ(2u, GetProviderServices().size());
  // Return the only |LastOnline|.
  ASSERT_EQ(
      11,
      provider()->FindLastOnline().ToDeltaSinceWindowsEpoch().InMilliseconds());
  provider()->RemoveServices();
  EXPECT_EQ(0u, GetProviderServices().size());

  // Both services have been online.
  storage_.SetUint64("cellular2", Service::kStorageLastOnline, 12);
  provider()->LoadServicesForDevice(device.get());
  provider()->LoadServicesForSecondarySim(kEid1, "iccid2", "imsi2",
                                          device.get());
  ASSERT_EQ(2u, GetProviderServices().size());
  // Return the latest |LastOnline|.
  ASSERT_EQ(
      12,
      provider()->FindLastOnline().ToDeltaSinceWindowsEpoch().InMilliseconds());
}

TEST_F(CellularServiceProviderTest, RemoveObsoleteServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  // Add two entries in the storage with the same ICCID, one with an empty IMSI.
  // Set a property on both.
  SetupCellularStore(identifier, "", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user1");
  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");
  StoreCellularProperty(identifier, CellularService::kStoragePPPUsername,
                        "user2");

  // Ensure that the service with a non empty imsi loaded from storage.
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  provider()->RemoveNonDeviceServices(device.get());
  ASSERT_TRUE(service);
  EXPECT_EQ("imsi1", service->imsi());
  EXPECT_EQ("iccid1", service->iccid());
  EXPECT_EQ("user2", service->ppp_username());

  // Only one provider service should exist.
  EXPECT_EQ(1u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, LoadServicesForSecondarySim) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  provider()->LoadServicesForDevice(device.get());
  EXPECT_EQ(1u, GetProviderServices().size());

  // Setup eSIM profiles on the secondary SIM, with iccid2 being the enabled
  // profile. iccid3 should not be loaded.
  std::string identifier = device->GetStorageIdentifier();
  SetupCellularStore(identifier, "imsi2", "iccid2", kEid1);
  SetupCellularStore(identifier, "imsi3", "iccid3", kEid1);
  provider()->LoadServicesForSecondarySim(kEid1, "iccid2", "imsi2",
                                          device.get());
  // Only the active ICCIDs (iccid1 and iccid2) should be loaded.
  EXPECT_EQ(2u, GetProviderServices().size());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryService) {
  KeyValueStore args;

  args.Set(CellularService::kStorageIccid, std::string("iccid1"));

  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryService(args, &error);

  ASSERT_TRUE(service);
  ASSERT_TRUE(error.IsSuccess());
  EXPECT_EQ("iccid1", AsCellularService(service)->iccid());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceNoIccid) {
  KeyValueStore args;

  args.Set(CellularService::kStorageImsi, std::string("imsi1"));

  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryService(args, &error);

  ASSERT_FALSE(service);
  ASSERT_FALSE(error.IsSuccess());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceWithImsi) {
  KeyValueStore args;

  args.Set(CellularService::kStorageIccid, std::string("iccid1"));
  args.Set(CellularService::kStorageImsi, std::string("imsi1"));

  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryService(args, &error);

  ASSERT_TRUE(service);
  ASSERT_TRUE(error.IsSuccess());

  CellularService* cellular_service = AsCellularService(service);
  EXPECT_EQ("iccid1", cellular_service->iccid());
  EXPECT_EQ("imsi1", cellular_service->imsi());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceWithSimCardId) {
  KeyValueStore args;

  args.Set(CellularService::kStorageIccid, std::string("iccid1"));
  args.Set(CellularService::kStorageSimCardId, std::string("iccid1"));

  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryService(args, &error);

  ASSERT_TRUE(service);
  ASSERT_TRUE(error.IsSuccess());

  // SIM card ID is the ICCID, so it shouldn't set any other identifiers.
  CellularService* cellular_service = AsCellularService(service);
  EXPECT_EQ("iccid1", cellular_service->iccid());
  EXPECT_EQ("", cellular_service->imsi());
  EXPECT_EQ("", cellular_service->eid());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceWithEid) {
  KeyValueStore args;

  args.Set(CellularService::kStorageIccid, std::string("iccid1"));
  args.Set(CellularService::kStorageSimCardId, std::string(kEid1));

  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryService(args, &error);

  ASSERT_TRUE(service);
  ASSERT_TRUE(error.IsSuccess());

  // SIM card ID is not the ICCID, and it looks like an EID, so we assume
  // it is the EID.
  CellularService* cellular_service = AsCellularService(service);
  EXPECT_EQ("iccid1", cellular_service->iccid());
  EXPECT_EQ("", cellular_service->imsi());
  EXPECT_EQ(kEid1, cellular_service->eid());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceWithUnusedSimCardId) {
  KeyValueStore args;

  args.Set(CellularService::kStorageIccid, std::string("iccid1"));
  args.Set(CellularService::kStorageSimCardId, std::string("sim_card_id"));

  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryService(args, &error);

  ASSERT_TRUE(service);
  ASSERT_TRUE(error.IsSuccess());

  // SIM card ID is neither the ICCID nor does it look like an EID. So we don't
  // use it.
  CellularService* cellular_service = AsCellularService(service);
  EXPECT_EQ("iccid1", cellular_service->iccid());
  EXPECT_EQ("", cellular_service->imsi());
  EXPECT_EQ("", cellular_service->eid());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceFromProfile) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  SetupCellularStore(identifier, "imsi1", "iccid1", "iccid1");

  // Ensure that the service is loaded from storage.
  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryServiceFromProfile(
      profile(), identifier, &error);

  ASSERT_TRUE(service);
  ASSERT_TRUE(error.IsSuccess());

  CellularService* cellular_service = AsCellularService(service);
  EXPECT_EQ("iccid1", cellular_service->iccid());
  EXPECT_EQ("imsi1", cellular_service->imsi());
}

TEST_F(CellularServiceProviderTest, CreateTemporaryServiceFromProfileNoIccid) {
  CellularRefPtr device = CreateDevice("imsi1", "iccid1");
  std::string identifier = device->GetStorageIdentifier();

  SetupCellularStore(identifier, "imsi1", "", "");

  // Ensure that the service is loaded from storage.
  Error error;
  ServiceRefPtr service = provider()->CreateTemporaryServiceFromProfile(
      profile(), identifier, &error);

  ASSERT_FALSE(service);
  ASSERT_FALSE(error.IsSuccess());
}

TEST_F(CellularServiceProviderTest, AcquireTetheringNetwork) {
  StrictMock<base::MockOnceCallback<void(TetheringManager::SetEnabledResult,
                                         Network*, ServiceRefPtr)>>
      cb;
  StrictMock<base::MockRepeatingCallback<void(base::TimeDelta)>>
      update_timeout_cb;
  StrictMock<base::MockRepeatingCallback<void(
      TetheringManager::CellularUpstreamEvent)>>
      upstream_event_cb;

  SetVariantThatSupportsTethering();
  // No Device registered.
  EXPECT_CALL(cb, Run(TetheringManager::SetEnabledResult::kNotAllowed, nullptr,
                      ServiceRefPtr()));
  EXPECT_CALL(update_timeout_cb, Run(_)).Times(0);
  provider()->AcquireTetheringNetwork(update_timeout_cb.Get(), cb.Get(),
                                      upstream_event_cb.Get(),
                                      /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);

  // Set up a Cellular Service with a Device
  scoped_refptr<MockCellular> device =
      new MockCellular(&manager_, kTestDeviceName, kTestDeviceAddress,
                       kTestInterfaceIndex, kDBusService, kDBusPath);
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  service->SetState(Service::kStateConnected);
  ON_CALL(*(device.get()), FirmwareSupportsTethering())
      .WillByDefault(Return(true));

  // The tethering network acquisition in the device fails
  EXPECT_CALL(*(device.get()), AcquireTetheringNetwork(_, _, _, _))
      .WillOnce(WithArgs<1>(
          Invoke([](Cellular::AcquireTetheringNetworkResultCallback callback) {
            std::move(callback).Run(nullptr, Error(Error::kOperationFailed));
          })));
  EXPECT_CALL(
      cb, Run(TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable,
              nullptr, ServiceRefPtr()));
  provider()->AcquireTetheringNetwork(TetheringManager::UpdateTimeoutCallback(),
                                      cb.Get(), upstream_event_cb.Get(),
                                      /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(device.get());
  Mock::VerifyAndClearExpectations(&cb);

  // The tethering network acquisition in the device succeeds
  // but for some reason no Network is returned
  EXPECT_CALL(*(device.get()), AcquireTetheringNetwork(_, _, _, _))
      .WillOnce(WithArgs<1>(
          Invoke([](Cellular::AcquireTetheringNetworkResultCallback callback) {
            std::move(callback).Run(nullptr, Error(Error::kSuccess));
          })));
  EXPECT_CALL(
      cb, Run(TetheringManager::SetEnabledResult::kUpstreamNetworkNotAvailable,
              nullptr, ServiceRefPtr()));
  provider()->AcquireTetheringNetwork(TetheringManager::UpdateTimeoutCallback(),
                                      cb.Get(), upstream_event_cb.Get(),
                                      /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(device.get());
  Mock::VerifyAndClearExpectations(&cb);

  // Setup a tethering Network to be returned
  std::unique_ptr<MockNetwork> network = std::make_unique<MockNetwork>(
      kTestInterfaceIndex, kTestDeviceName, Technology::kCellular);
  auto network_ptr = network.get();
  service->AttachNetwork(network->AsWeakPtr());

  // The tethering network acquisition in the device succeeds
  // and a valid Network is returned
  EXPECT_CALL(*(device.get()), AcquireTetheringNetwork(_, _, _, _))
      .WillOnce(WithArgs<1>(
          Invoke([network_ptr](
                     Cellular::AcquireTetheringNetworkResultCallback callback) {
            std::move(callback).Run(network_ptr, Error(Error::kSuccess));
          })));
  EXPECT_CALL(cb, Run(TetheringManager::SetEnabledResult::kSuccess, network_ptr,
                      ServiceRefPtr(service)));
  provider()->AcquireTetheringNetwork(TetheringManager::UpdateTimeoutCallback(),
                                      cb.Get(), upstream_event_cb.Get(),
                                      /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(device.get());
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(CellularServiceProviderTest,
       AcquireTetheringNetwork_HardwareDoesNotSupportTethering) {
  StrictMock<base::MockOnceCallback<void(TetheringManager::SetEnabledResult,
                                         Network*, ServiceRefPtr)>>
      cb;
  StrictMock<base::MockRepeatingCallback<void(
      TetheringManager::CellularUpstreamEvent)>>
      upstream_event_cb;
  SetVariantThatDoesNotSupportTethering();
  // No Device registered.
  EXPECT_CALL(cb, Run(TetheringManager::SetEnabledResult::kNotAllowed, nullptr,
                      ServiceRefPtr()));
  provider()->AcquireTetheringNetwork(TetheringManager::UpdateTimeoutCallback(),
                                      cb.Get(), upstream_event_cb.Get(),
                                      /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(CellularServiceProviderTest, HardwareSupportsTetheringReturnsTrue) {
  SetVariantThatSupportsTethering();
  // Set up a Cellular Service with a Device.
  scoped_refptr<MockCellular> device =
      new MockCellular(&manager_, kTestDeviceName, kTestDeviceAddress,
                       kTestInterfaceIndex, kDBusService, kDBusPath);
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  service->SetState(Service::kStateConnected);
  ON_CALL(*(device.get()), FirmwareSupportsTethering())
      .WillByDefault(Return(true));

  EXPECT_TRUE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/false));
  EXPECT_TRUE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/true));
}

TEST_F(CellularServiceProviderTest,
       HardwareSupportsTetheringVariantNotSupported) {
  SetVariantThatDoesNotSupportTethering();
  // Set up a Cellular Service with a Device
  scoped_refptr<MockCellular> device =
      new MockCellular(&manager_, kTestDeviceName, kTestDeviceAddress,
                       kTestInterfaceIndex, kDBusService, kDBusPath);
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  service->SetState(Service::kStateConnected);
  ON_CALL(*(device.get()), FirmwareSupportsTethering())
      .WillByDefault(Return(true));

  EXPECT_FALSE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/false));
  EXPECT_TRUE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/true));
}

TEST_F(CellularServiceProviderTest, HardwareSupportsTetheringFwNotSupported) {
  SetVariantThatSupportsTethering();
  // Set up a Cellular Service with a Device
  scoped_refptr<MockCellular> device =
      new MockCellular(&manager_, kTestDeviceName, kTestDeviceAddress,
                       kTestInterfaceIndex, kDBusService, kDBusPath);
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  service->SetState(Service::kStateConnected);
  EXPECT_CALL(*(device.get()), FirmwareSupportsTethering())
      .WillOnce(Return(false));
  EXPECT_FALSE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/false));
  EXPECT_TRUE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/true));
}

TEST_F(CellularServiceProviderTest, HardwareSupportsTetheringNoCellularDevice) {
  SetVariantThatSupportsTethering();
  EXPECT_FALSE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/false));
  EXPECT_FALSE(
      provider()->HardwareSupportsTethering(/*experimental_tethering=*/true));
}

TEST_F(CellularServiceProviderTest, TetheringEntitlementCheck_Ok) {
  StrictMock<base::MockOnceCallback<void(TetheringManager::EntitlementStatus)>>
      cb;
  SetVariantThatSupportsTethering();

  // Set up a Cellular Service with a Device
  scoped_refptr<MockCellular> device =
      new MockCellular(&manager_, kTestDeviceName, kTestDeviceAddress,
                       kTestInterfaceIndex, kDBusService, kDBusPath);
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  service->SetState(Service::kStateConnected);
  ON_CALL(*(device.get()), FirmwareSupportsTethering())
      .WillByDefault(Return(true));

  EXPECT_CALL(*(device.get()), EntitlementCheck(_, _))
      .WillOnce(WithArgs<0>(
          Invoke([](Cellular::EntitlementCheckResultCallback callback) {
            std::move(callback).Run(
                TetheringManager::EntitlementStatus::kReady);
          })));
  EXPECT_CALL(cb, Run(TetheringManager::EntitlementStatus::kReady));
  provider()->TetheringEntitlementCheck(cb.Get(),
                                        /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(CellularServiceProviderTest,
       TetheringEntitlementCheck_VariantNotAllowed) {
  StrictMock<base::MockOnceCallback<void(TetheringManager::EntitlementStatus)>>
      cb;
  SetVariantThatDoesNotSupportTethering();
  EXPECT_CALL(cb,
              Run(TetheringManager::EntitlementStatus::kNotAllowedOnVariant));
  provider()->TetheringEntitlementCheck(cb.Get(),
                                        /*experimental_tethering=*/false);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(&cb);
}

TEST_F(CellularServiceProviderTest,
       TetheringEntitlementCheck_OverrideVariantNotAllowed) {
  StrictMock<base::MockOnceCallback<void(TetheringManager::EntitlementStatus)>>
      cb;
  SetVariantThatDoesNotSupportTethering();
  // Set up a Cellular Service with a Device
  scoped_refptr<MockCellular> device =
      new MockCellular(&manager_, kTestDeviceName, kTestDeviceAddress,
                       kTestInterfaceIndex, kDBusService, kDBusPath);
  CellularServiceRefPtr service =
      provider()->LoadServicesForDevice(device.get());
  service->SetState(Service::kStateConnected);
  ON_CALL(*(device.get()), FirmwareSupportsTethering())
      .WillByDefault(Return(true));

  EXPECT_CALL(*(device.get()), EntitlementCheck(_, _))
      .WillOnce(WithArgs<0>(
          Invoke([](Cellular::EntitlementCheckResultCallback callback) {
            std::move(callback).Run(
                TetheringManager::EntitlementStatus::kReady);
          })));
  EXPECT_CALL(cb, Run(TetheringManager::EntitlementStatus::kReady));
  provider()->TetheringEntitlementCheck(cb.Get(),
                                        /*experimental_tethering=*/true);
  DispatchPendingEvents();
  Mock::VerifyAndClearExpectations(device.get());
  Mock::VerifyAndClearExpectations(&cb);
}
}  // namespace shill
