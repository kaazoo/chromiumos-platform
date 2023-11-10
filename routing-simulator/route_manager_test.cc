// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "routing-simulator/route_manager.h"

#include <string>
#include <string_view>

#include <base/strings/string_split.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <net-base/ip_address.h>

#include "routing-simulator/mock_process_executor.h"
#include "routing-simulator/routing_policy_entry.h"
#include "routing-simulator/routing_table.h"

namespace routing_simulator {
namespace {

using ::testing::Return;

constexpr std::string_view kMockIpRuleOutputIpv4 = R"(0: from all lookup local
1000:           from all lookup main
1010: from all fwmark 0x3ea0000/0xffff0000 lookup 1002
1010: from 100.87.84.132/24 lookup 1002
1010: from all iif eth0 lookup 1002
1020: from all fwmark 0x3eb0000/0xffff0000 lookup 1003
1020: from all oif wlan0 lookup 1003
1020: from 100.86.210.153/22 lookup 1003
1020: from all iif wlan0 lookup 1003
32763: from 100.115.92.24/29 lookup 249
32763: from 100.115.92.32/27 lookup 249
32763: from 100.115.92.192/26 lookup 249
32765: from all lookup 1002
32766: from all lookup main
32767: from all lookup default)";

constexpr std::string_view kMockIpRuleOutputIpv6 = R"(0: from all lookup local
1000: from all lookup main
1010: from all fwmark 0x3ea0000/0xffff0000 lookup 1002
1010: from 2401:fa00:480:ee08:20e:c6ff:fe63:5c3f/64 lookup 1002
1010: from all iif eth0 lookup 1002
1020: from all oif wlan0 lookup 1003
1020: from 2a00:79e1:abc:f604:faac:65ff:fe56:100d/64 lookup 1003
1020: from 2a00:79e1:abc:f604:41d0:1fad:f561:15d8/64 lookup 1003
1020: from all iif wlan0 lookup 1003
32765: from all lookup 1002
32766: from all lookup main)";

constexpr std::string_view kMockIpRouteOutputIpv4 =
    R"(default via 100.87.84.254 dev eth0 table 1002 metric 65536
default via 100.86.211.254 dev wlan0 table 1003 metric 65536
unreachable default table 250
100.86.208.0/22 dev wlan0 proto kernel scope link src 100.86.210.153
100.115.92.132/30 dev arc_ns1 proto kernel scope link src 100.115.92.133
local 100.86.210.153 dev wlan0 table local proto kernel scope host src 100.86.210.153
broadcast 100.86.211.255 dev wlan0 table local proto kernel scope link src 100.86.210.153)";

constexpr std::string_view kMockIpRouteOutputIpv6 =
    R"(2401:fa00:480:ee08::/64 dev eth0 table 1002 proto kernel metric 256 expires 2591823sec pref medium
2a00:79e1:abc:f604::/64 dev wlan0 table 1003 proto kernel metric 256 expires 2591735sec pref medium
default via fe80::2a00:79e1:abc:f604 dev wlan0 table 1003 proto ra metric 1024 expires 3335sec hoplimit 64 pref medium
unreachable default dev lo table 250 metric 1024 pref medium
fdb9:72a:70c5:959d::/64 dev arc_ns1 proto kernel metric 256 pref medium
local ::1 dev lo table local proto kernel metric 0 pref medium
anycast 2401:fa00:480:ee08:: dev eth0 table local proto kernel metric 0 pref medium
multicast ff00::/8 dev wlan0 table local proto kernel metric 256 pref medium)";

const std::map<std::string, std::vector<std::string_view>>
    kExpectedTableToRoutesIpv4 = {
        {"1002",
         {"default via 100.87.84.254 dev eth0 table 1002 metric 65536"}},
        {"1003",
         {"default via 100.86.211.254 dev wlan0 table 1003 metric 65536"}},
        {"250", {"unreachable default table 250"}},
        {"main",
         {"100.86.208.0/22 dev wlan0 proto kernel scope link src "
          "100.86.210.153",
          "100.115.92.132/30 dev arc_ns1 proto kernel scope link src "
          "100.115.92.133"}},
        {"local",
         {"local 100.86.210.153 dev wlan0 table local proto kernel scope "
          "host "
          "src 100.86.210.153",
          "broadcast 100.86.211.255 dev wlan0 table local proto kernel "
          "scope "
          "link src 100.86.210.153"}}};

const std::map<std::string, std::vector<std::string_view>>
    kExpectedTableToRoutesIpv6 = {
        {"1002",
         {"2401:fa00:480:ee08::/64 dev eth0 table 1002 proto kernel metric "
          "256 "
          "expires 2591823sec pref medium"}},
        {"1003",
         {
             "2a00:79e1:abc:f604::/64 dev wlan0 table 1003 proto kernel metric "
             "256 expires 2591735sec pref medium",
             "default via fe80::2a00:79e1:abc:f604 dev wlan0 table 1003 proto "
             "ra metric 1024 expires 3335sec hoplimit 64 pref medium",
         }},
        {"main",
         {"fdb9:72a:70c5:959d::/64 dev arc_ns1 proto kernel metric 256 pref "
          "medium"}},
        {"local",
         {"local ::1 dev lo table local proto kernel metric 0 pref medium",
          "anycast 2401:fa00:480:ee08:: dev eth0 table local proto kernel "
          "metric 0 pref medium",
          "multicast ff00::/8 dev wlan0 table local proto kernel metric 256 "
          "pref medium"}},
        {"250",
         {"unreachable default dev lo table 250 metric 1024 pref medium"}}};

// Creates an expected result of routing policy table by BuildTables().
std::vector<RoutingPolicyEntry> CreateRoutingPolicyTable(
    std::string_view policies, net_base::IPFamily ip_family) {
  std::vector<RoutingPolicyEntry> routing_policy_table_expected;
  const auto policy_lines = base::SplitStringPiece(
      policies, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& policy_str : policy_lines) {
    const auto policy =
        RoutingPolicyEntry::CreateFromPolicyString(policy_str, ip_family);
    CHECK(policy.has_value());
    routing_policy_table_expected.push_back(*policy);
  }
  return routing_policy_table_expected;
}

// Creates an expected result of routing table by BuildTables().
std::map<std::string, RoutingTable> CreateRoutingTable(
    std::map<std::string, std::vector<std::string_view>> table_to_routes,
    net_base::IPFamily ip_family) {
  std::map<std::string, RoutingTable> routing_tables_expected;
  for (const auto& routing_table_case : table_to_routes) {
    const auto table_id = routing_table_case.first;
    const auto routes = routing_table_case.second;
    RoutingTable routing_table;
    for (const auto& route_str : routes) {
      const auto route = Route::CreateFromRouteString(route_str, ip_family);
      CHECK(route.has_value());
      routing_table.AddRoute(*route);
    }
    routing_tables_expected.emplace(table_id, routing_table);
  }
  return routing_tables_expected;
}

TEST(RouteManagerTest, BuildTablesTest) {
  MockProcessExecutor process_executor;
  // Execute 'ip -4 rule show'.
  EXPECT_CALL(process_executor,
              RunAndGetStdout(base::FilePath("/bin/ip"),
                              std::vector<std::string>{"-4", "rule", "show"}))
      .WillOnce(Return(std::string(kMockIpRuleOutputIpv4)));

  // Execute 'ip -6 rule show'.
  EXPECT_CALL(process_executor,
              RunAndGetStdout(base::FilePath("/bin/ip"),
                              std::vector<std::string>{"-6", "rule", "show"}))
      .WillOnce(Return(std::string(kMockIpRuleOutputIpv6)));

  // Execute 'ip -4 route show table all'.
  EXPECT_CALL(process_executor,
              RunAndGetStdout(base::FilePath("/bin/ip"),
                              std::vector<std::string>{"-4", "route", "show",
                                                       "table", "all"}))
      .WillOnce(Return(std::string(kMockIpRouteOutputIpv4)));

  // Execute 'ip -6 route show table all'.
  EXPECT_CALL(process_executor,
              RunAndGetStdout(base::FilePath("/bin/ip"),
                              std::vector<std::string>{"-6", "route", "show",
                                                       "table", "all"}))
      .WillOnce(Return(std::string(kMockIpRouteOutputIpv6)));

  RouteManager route_manager(&process_executor);
  route_manager.BuildTables();
  // Test a routing policy table for IPv4.
  const auto routing_policy_table_ipv4_expected = CreateRoutingPolicyTable(
      kMockIpRuleOutputIpv4, net_base::IPFamily::kIPv4);
  EXPECT_EQ(route_manager.routing_policy_table_ipv4(),
            routing_policy_table_ipv4_expected);

  // Test a routing policy table for IPv6.
  const auto routing_policy_table_ipv6_expected = CreateRoutingPolicyTable(
      kMockIpRuleOutputIpv6, net_base::IPFamily::kIPv6);
  EXPECT_EQ(route_manager.routing_policy_table_ipv6(),
            routing_policy_table_ipv6_expected);

  // Test a routing table for IPv4.
  auto routing_tables_ipv4_expected =
      CreateRoutingTable(kExpectedTableToRoutesIpv4, net_base::IPFamily::kIPv4);
  for (const auto& [table_id, routing_table_actual] :
       route_manager.routing_tables_ipv4()) {
    ASSERT_TRUE(routing_tables_ipv4_expected.contains(table_id));
    EXPECT_EQ(routing_table_actual, routing_tables_ipv4_expected[table_id]);
  }

  // Test a routing table for IPv6.
  auto routing_tables_ipv6_expected =
      CreateRoutingTable(kExpectedTableToRoutesIpv6, net_base::IPFamily::kIPv6);
  for (const auto& [table_id, routing_table_actual] :
       route_manager.routing_tables_ipv6()) {
    ASSERT_TRUE(routing_tables_ipv6_expected.contains(table_id));
    EXPECT_EQ(routing_table_actual, routing_tables_ipv6_expected[table_id]);
  }
}

// TODO(b/307460180): Add implementations
TEST(RouteManagerTest, ProcessPacketTest) {}

}  // namespace
}  // namespace routing_simulator
