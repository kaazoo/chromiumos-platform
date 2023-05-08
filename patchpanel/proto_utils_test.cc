// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/proto_utils.h"

#include <memory>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/crostini_service.h"
#include "patchpanel/device.h"

namespace patchpanel {

class ProtoUtilsTest : public testing::Test {
 protected:
  void SetUp() override { addr_mgr_ = std::make_unique<AddressManager>(); }

  std::unique_ptr<AddressManager> addr_mgr_;
};

TEST_F(ProtoUtilsTest, ConvertTerminaDevice) {
  const uint32_t subnet_index = 0;
  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      AddressManager::GuestType::kTerminaVM, subnet_index);
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  auto lxd_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kLXDContainer);
  auto expected_host_ipv4 = host_ipv4_addr->Address();
  auto expected_guest_ipv4 = guest_ipv4_addr->Address();

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr), std::move(lxd_subnet));
  auto device = std::make_unique<Device>(Device::Type::kTerminaVM, "vmtap0",
                                         "vmtap0", "", std::move(config));

  NetworkDevice proto_device;
  FillDeviceProto(*device, &proto_device);

  ASSERT_EQ("vmtap0", proto_device.ifname());
  // Convention for Crostini Devices is to reuse the virtual interface name in
  // place of the interface name of the upstream network used by ARC Devices.
  ASSERT_EQ("vmtap0", proto_device.phys_ifname());
  ASSERT_EQ("", proto_device.guest_ifname());
  ASSERT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  ASSERT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  ASSERT_EQ(NetworkDevice::TERMINA_VM, proto_device.guest_type());
  // LXD container ??
}

TEST_F(ProtoUtilsTest, ConvertParallelDevice) {
  const uint32_t subnet_index = 1;
  const auto mac_addr = addr_mgr_->GenerateMacAddress(subnet_index);
  auto ipv4_subnet = addr_mgr_->AllocateIPv4Subnet(
      AddressManager::GuestType::kPluginVM, subnet_index);
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  auto expected_host_ipv4 = host_ipv4_addr->Address();
  auto expected_guest_ipv4 = guest_ipv4_addr->Address();

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));
  auto device = std::make_unique<Device>(Device::Type::kParallelVM, "vmtap1",
                                         "vmtap1", "", std::move(config));

  NetworkDevice proto_device;
  FillDeviceProto(*device, &proto_device);

  ASSERT_EQ("vmtap1", proto_device.ifname());
  // Convention for Crostini Devices is to reuse the virtual interface name in
  // place of the interface name of the upstream network used by ARC Devices.
  ASSERT_EQ("vmtap1", proto_device.phys_ifname());
  ASSERT_EQ("", proto_device.guest_ifname());
  ASSERT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  ASSERT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  ASSERT_EQ(NetworkDevice::PLUGIN_VM, proto_device.guest_type());
}

TEST_F(ProtoUtilsTest, ConvertARCContainerDevice) {
  const auto mac_addr = addr_mgr_->GenerateMacAddress(0);
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArcNet, 0);
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  auto expected_host_ipv4 = host_ipv4_addr->Address();
  auto expected_guest_ipv4 = guest_ipv4_addr->Address();

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));
  auto device =
      std::make_unique<Device>(Device::Type::kARCContainer, "wlan0",
                               "arc_wlan0", "wlan0", std::move(config));

  NetworkDevice proto_device;
  FillDeviceProto(*device, &proto_device);

  ASSERT_EQ("arc_wlan0", proto_device.ifname());
  ASSERT_EQ("wlan0", proto_device.phys_ifname());
  // For ARC container, the name of the veth half set inside the container is
  // renamed to match the name of the host upstream network interface managed by
  // shill.
  ASSERT_EQ("wlan0", proto_device.guest_ifname());
  ASSERT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  ASSERT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  ASSERT_EQ(NetworkDevice::ARC, proto_device.guest_type());
}

TEST_F(ProtoUtilsTest, ConvertARCVMDevice) {
  const auto mac_addr = addr_mgr_->GenerateMacAddress(3);
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArcNet, 0);
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  auto expected_host_ipv4 = host_ipv4_addr->Address();
  auto expected_guest_ipv4 = guest_ipv4_addr->Address();

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));
  auto device = std::make_unique<Device>(
      Device::Type::kARCVM, "wlan0", "arc_wlan0", "eth3", std::move(config));

  NetworkDevice proto_device;
  FillDeviceProto(*device, &proto_device);

  ASSERT_EQ("arc_wlan0", proto_device.ifname());
  ASSERT_EQ("wlan0", proto_device.phys_ifname());
  // For ARCVM, the name of the virtio interface is controlled by the virtio
  // driver and follows a ethernet-like pattern.
  ASSERT_EQ("eth3", proto_device.guest_ifname());
  ASSERT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  ASSERT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  ASSERT_EQ(NetworkDevice::ARCVM, proto_device.guest_type());
}

TEST_F(ProtoUtilsTest, ConvertARC0ForARCContainer) {
  const auto mac_addr = addr_mgr_->GenerateMacAddress(0);
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArc0, 0);
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  auto expected_host_ipv4 = host_ipv4_addr->Address();
  auto expected_guest_ipv4 = guest_ipv4_addr->Address();

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));
  auto device = std::make_unique<Device>(Device::Type::kARC0, "arc0", "arcbr0",
                                         "arc0", std::move(config));

  NetworkDevice proto_device;
  FillDeviceProto(*device, &proto_device);

  ASSERT_EQ("arcbr0", proto_device.ifname());
  // Convention for arc0 is to reuse the virtual interface name in
  // place of the interface name of the upstream network used by other ARC
  // Devices.
  ASSERT_EQ("arc0", proto_device.phys_ifname());
  // For arc0 with ARC container, the name of the veth half inside ARC is set
  // to "arc0" for legacy compatibility with old ARC N code, and ARC P code
  // prior to ARC multinetworking support.
  ASSERT_EQ("arc0", proto_device.guest_ifname());
  ASSERT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  ASSERT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  ASSERT_EQ(NetworkDevice::UNKNOWN, proto_device.guest_type());
}

TEST_F(ProtoUtilsTest, ConvertARC0ForARCVM) {
  const auto mac_addr = addr_mgr_->GenerateMacAddress(0);
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArc0, 0);
  auto host_ipv4_addr = ipv4_subnet->AllocateAtOffset(0);
  auto guest_ipv4_addr = ipv4_subnet->AllocateAtOffset(1);
  auto expected_host_ipv4 = host_ipv4_addr->Address();
  auto expected_guest_ipv4 = guest_ipv4_addr->Address();

  auto config = std::make_unique<Device::Config>(
      mac_addr, std::move(ipv4_subnet), std::move(host_ipv4_addr),
      std::move(guest_ipv4_addr));
  auto device = std::make_unique<Device>(Device::Type::kARC0, "arc0", "arcbr0",
                                         "arc0", std::move(config));

  NetworkDevice proto_device;
  FillDeviceProto(*device, &proto_device);

  ASSERT_EQ("arcbr0", proto_device.ifname());
  // Convention for arc0 is to reuse the virtual interface name in
  // place of the interface name of the upstream network used by other ARC
  // Devices.
  ASSERT_EQ("arc0", proto_device.phys_ifname());
  // For arc0 with ARC container, the name of the veth half inside ARC is set
  // to "arc0" for legacy compatibility with old ARC N code, and ARC P code
  // prior to ARC multinetworking support.
  ASSERT_EQ("arc0", proto_device.guest_ifname());
  ASSERT_EQ(expected_guest_ipv4, proto_device.ipv4_addr());
  ASSERT_EQ(expected_host_ipv4, proto_device.host_ipv4_addr());
  ASSERT_EQ(NetworkDevice::UNKNOWN, proto_device.guest_type());
}
}  // namespace patchpanel
