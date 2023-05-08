// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/address_manager.h"

#include <map>
#include <utility>
#include <vector>

#include <arpa/inet.h>

#include "patchpanel/net_util.h"

#include <base/rand_util.h>
#include <gtest/gtest.h>

namespace patchpanel {

using GuestType = AddressManager::GuestType;

TEST(AddressManager, BaseAddresses) {
  std::map<GuestType, size_t> addrs = {
      {GuestType::kArc0, Ipv4Addr(100, 115, 92, 0)},
      {GuestType::kArcNet, Ipv4Addr(100, 115, 92, 4)},
      {GuestType::kTerminaVM, Ipv4Addr(100, 115, 92, 24)},
      {GuestType::kPluginVM, Ipv4Addr(100, 115, 93, 0)},
      {GuestType::kLXDContainer, Ipv4Addr(100, 115, 92, 192)},
      {GuestType::kNetns, Ipv4Addr(100, 115, 92, 128)},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    ASSERT_TRUE(subnet != nullptr);
    // The first address (offset 0) returned by Subnet is not the base address,
    // rather it's the first usable IP address... so the base is 1 less.
    EXPECT_EQ(a.second, htonl(ntohl(subnet->AddressAtOffset(0)) - 1));
  }
}

TEST(AddressManager, AddressesPerSubnet) {
  std::map<GuestType, size_t> addrs = {
      {GuestType::kArc0, 2},          {GuestType::kArcNet, 2},
      {GuestType::kTerminaVM, 2},     {GuestType::kPluginVM, 6},
      {GuestType::kLXDContainer, 14}, {GuestType::kNetns, 2},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    ASSERT_TRUE(subnet != nullptr);
    EXPECT_EQ(a.second, subnet->AvailableCount());
  }
}

TEST(AddressManager, SubnetsPerPool) {
  std::map<GuestType, size_t> addrs = {
      {GuestType::kArc0, 1},         {GuestType::kArcNet, 5},
      {GuestType::kTerminaVM, 26},   {GuestType::kPluginVM, 32},
      {GuestType::kLXDContainer, 4}, {GuestType::kNetns, 16},
  };
  AddressManager mgr;
  for (const auto a : addrs) {
    std::vector<std::unique_ptr<Subnet>> subnets;
    for (size_t i = 0; i < a.second; ++i) {
      auto subnet = mgr.AllocateIPv4Subnet(a.first);
      EXPECT_TRUE(subnet != nullptr);
      subnets.emplace_back(std::move(subnet));
    }
    auto subnet = mgr.AllocateIPv4Subnet(a.first);
    EXPECT_TRUE(subnet == nullptr);
  }
}

TEST(AddressManager, SubnetIndexing) {
  AddressManager mgr;
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kArc0, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kArcNet, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kTerminaVM, 1));
  EXPECT_TRUE(mgr.AllocateIPv4Subnet(GuestType::kPluginVM, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kLXDContainer, 1));
  EXPECT_FALSE(mgr.AllocateIPv4Subnet(GuestType::kNetns, 1));
}

TEST(AddressManager, StableMacAddresses) {
  AddressManager mgr;
  EXPECT_NE(mgr.GenerateMacAddress(), mgr.GenerateMacAddress());
  EXPECT_NE(mgr.GenerateMacAddress(kAnySubnetIndex),
            mgr.GenerateMacAddress(kAnySubnetIndex));
  for (int i = 0; i < 100; ++i) {
    uint8_t index = 0;
    while (index == 0) {
      base::RandBytes(&index, 1);
    }
    EXPECT_EQ(mgr.GenerateMacAddress(index), mgr.GenerateMacAddress(index));
  }
}

}  // namespace patchpanel
