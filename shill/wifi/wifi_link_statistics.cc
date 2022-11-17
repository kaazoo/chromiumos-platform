// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/wifi/wifi_link_statistics.h"

#include <string>
#include <utility>
#include <vector>

#include <chromeos/dbus/service_constants.h>

namespace shill {
namespace {
// Determine if the WiFi link statistics should be print to log.
bool ShouldPrintWiFiLinkStatistics(
    WiFiLinkStatistics::LinkStatisticsTrigger trigger) {
  // It doesn't consider if the service is connected (Service::IsConnected() ==
  // true) when determining if the WiFi link statistics should be printed.
  // There are two examples where the service is connected, but the necessity of
  // WiFi link statistics differs.

  // 1. For IPv6-only networks, the network event transition may be
  // kIPConfigurationStart -> kSlaacFinished -> kConnected -> kDHCPFailure, the
  // WiFi link statistics should not be printed.
  // 2. Suppose a device has a static IPv4 configuration but it still needs DHCP
  // to succeed (to obtain vendor options, like proxy settings) and DHCP fails
  // due to poor link connection, the WiFi link statistics should be printed.

  // It may print unnecessary WiFi link statistics if the state of the service
  // is not considered. It is acceptable because the size of the WiFi link
  // statistics in netlog is small.
  return trigger == WiFiLinkStatistics::LinkStatisticsTrigger::kDHCPFailure ||
         trigger == WiFiLinkStatistics::LinkStatisticsTrigger::
                        kNetworkValidationFailure;
}

bool IsEndNetworkEvent(WiFiLinkStatistics::LinkStatisticsTrigger trigger) {
  return trigger == WiFiLinkStatistics::LinkStatisticsTrigger::kConnected ||
         trigger == WiFiLinkStatistics::LinkStatisticsTrigger::kDHCPSuccess ||
         trigger == WiFiLinkStatistics::LinkStatisticsTrigger::kDHCPFailure ||
         trigger == WiFiLinkStatistics::LinkStatisticsTrigger::kSlaacFinished ||
         trigger == WiFiLinkStatistics::LinkStatisticsTrigger::
                        kNetworkValidationSuccess ||
         trigger == WiFiLinkStatistics::LinkStatisticsTrigger::
                        kNetworkValidationFailure;
}

bool DoesEndMatchStartEvent(
    WiFiLinkStatistics::LinkStatisticsTrigger start_event,
    WiFiLinkStatistics::LinkStatisticsTrigger end_event) {
  // kIPConfigurationStart is used to represent IPv4 and IPv6 configuration
  // start, so kConnected doesn't actually have a corresponding start event.
  switch (end_event) {
    case WiFiLinkStatistics::LinkStatisticsTrigger::kDHCPSuccess:
    case WiFiLinkStatistics::LinkStatisticsTrigger::kDHCPFailure:
      return start_event == WiFiLinkStatistics::LinkStatisticsTrigger::
                                kIPConfigurationStart ||
             start_event ==
                 WiFiLinkStatistics::LinkStatisticsTrigger::kDHCPRenewOnRoam;
    case WiFiLinkStatistics::LinkStatisticsTrigger::kSlaacFinished:
      return start_event ==
             WiFiLinkStatistics::LinkStatisticsTrigger::kIPConfigurationStart;
    case WiFiLinkStatistics::LinkStatisticsTrigger::kNetworkValidationSuccess:
    case WiFiLinkStatistics::LinkStatisticsTrigger::kNetworkValidationFailure:
      return start_event ==
             WiFiLinkStatistics::LinkStatisticsTrigger::kNetworkValidationStart;
    default:
      return false;
  }
}

// Calculate the difference between NL80211 link statistics old_stats and
// new_stats
WiFiLinkStatistics::StationStats Nl80211LinkStatisticsDiff(
    const WiFiLinkStatistics::StationStats& old_stats,
    const WiFiLinkStatistics::StationStats& new_stats) {
  WiFiLinkStatistics::StationStats diff_stats;
  diff_stats.rx.packets = new_stats.rx.packets - old_stats.rx.packets;
  diff_stats.tx.packets = new_stats.tx.packets - old_stats.tx.packets;
  diff_stats.rx.bytes = new_stats.rx.bytes - old_stats.rx.bytes;
  diff_stats.tx.bytes = new_stats.tx.bytes - old_stats.tx.bytes;
  diff_stats.tx_failed = new_stats.tx_failed - old_stats.tx_failed;
  diff_stats.tx_retries = new_stats.tx_retries - old_stats.tx_retries;
  diff_stats.rx_drop_misc = new_stats.rx_drop_misc - old_stats.rx_drop_misc;
  diff_stats.signal = new_stats.signal;
  diff_stats.signal_avg = new_stats.signal_avg;
  return diff_stats;
}

// Calculate the difference between RTNL link statistics old_stats and
// new_stats
old_rtnl_link_stats64 RtnlLinkStatisticsDiff(
    const old_rtnl_link_stats64& old_stats,
    const old_rtnl_link_stats64& new_stats) {
  old_rtnl_link_stats64 diff_stats;
  diff_stats.rx_packets = new_stats.rx_packets - old_stats.rx_packets;
  diff_stats.tx_packets = new_stats.tx_packets - old_stats.tx_packets;
  diff_stats.rx_bytes = new_stats.rx_bytes - old_stats.rx_bytes;
  diff_stats.tx_bytes = new_stats.tx_bytes - old_stats.tx_bytes;
  diff_stats.rx_errors = new_stats.rx_errors - old_stats.rx_errors;
  diff_stats.tx_errors = new_stats.tx_errors - old_stats.tx_errors;
  diff_stats.rx_dropped = new_stats.rx_dropped - old_stats.rx_dropped;
  diff_stats.tx_dropped = new_stats.tx_dropped - old_stats.tx_dropped;
  return diff_stats;
}

// Convert RTNL link statistics to string
std::string RtnlLinkStatisticsToString(
    const old_rtnl_link_stats64& diff_stats) {
  return "rx_packets " + std::to_string(diff_stats.rx_packets) +
         " tx_packets " + std::to_string(diff_stats.tx_packets) + " rx_bytes " +
         std::to_string(diff_stats.rx_bytes) + " tx_bytes " +
         std::to_string(diff_stats.tx_bytes) + " rx_errors " +
         std::to_string(diff_stats.rx_errors) + " tx_errors " +
         std::to_string(diff_stats.tx_errors) + " rx_dropped " +
         std::to_string(diff_stats.rx_dropped) + " tx_dropped " +
         std::to_string(diff_stats.tx_dropped);
}

// Convert NL80211 link statistics to string
std::string Nl80211LinkStatisticsToString(
    const WiFiLinkStatistics::StationStats& diff_stats) {
  return std::string(kPacketReceiveSuccessesProperty) + " " +
         std::to_string(diff_stats.rx.packets) + " " +
         kPacketTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx.packets) + " " +
         kByteReceiveSuccessesProperty + " " +
         std::to_string(diff_stats.rx.bytes) + " " +
         kByteTransmitSuccessesProperty + " " +
         std::to_string(diff_stats.tx.bytes) + " " +
         kPacketTransmitFailuresProperty + " " +
         std::to_string(diff_stats.tx_failed) + " " + kTransmitRetriesProperty +
         " " + std::to_string(diff_stats.tx_retries) + " " +
         kPacketReceiveDropProperty + " " +
         std::to_string(diff_stats.rx_drop_misc) +
         "; the current signal information: " + kLastReceiveSignalDbmProperty +
         " " + std::to_string(diff_stats.signal) + " " +
         kAverageReceiveSignalDbmProperty + " " +
         std::to_string(diff_stats.signal_avg);
}

WiFiLinkStatistics::StationStats ConvertNl80211StaInfo(
    const KeyValueStore& link_statistics) {
  WiFiLinkStatistics::StationStats stats;
  std::vector<std::pair<std::string, uint32_t*>>
      nl80211_sta_info_properties_u32 = {
          {kPacketReceiveSuccessesProperty, &stats.rx.packets},
          {kPacketTransmitSuccessesProperty, &stats.tx.packets},
          {kByteReceiveSuccessesProperty, &stats.rx.bytes},
          {kByteTransmitSuccessesProperty, &stats.tx.bytes},
          {kPacketTransmitFailuresProperty, &stats.tx_failed},
          {kTransmitRetriesProperty, &stats.tx_retries}};

  for (const auto& kv : nl80211_sta_info_properties_u32) {
    if (link_statistics.Contains<uint32_t>(kv.first)) {
      *kv.second = link_statistics.Get<uint32_t>(kv.first);
    }
  }

  std::vector<std::pair<std::string, uint64_t*>>
      nl80211_sta_info_properties_u64 = {
          {kPacketReceiveDropProperty, &stats.rx_drop_misc}};

  for (const auto& kv : nl80211_sta_info_properties_u64) {
    if (link_statistics.Contains<uint64_t>(kv.first)) {
      *kv.second = link_statistics.Get<uint64_t>(kv.first);
    }
  }

  std::vector<std::pair<std::string, int32_t*>>
      nl80211_sta_info_properties_s32 = {
          {kLastReceiveSignalDbmProperty, &stats.signal},
          {kAverageReceiveSignalDbmProperty, &stats.signal_avg}};

  for (const auto& kv : nl80211_sta_info_properties_s32) {
    if (link_statistics.Contains<int32_t>(kv.first)) {
      *kv.second = link_statistics.Get<int32_t>(kv.first);
    }
  }
  return stats;
}

}  // namespace

// static
std::string WiFiLinkStatistics::LinkStatisticsTriggerToString(
    LinkStatisticsTrigger trigger) {
  switch (trigger) {
    case LinkStatisticsTrigger::kUnknown:
      return "kUnknown";
    case LinkStatisticsTrigger::kIPConfigurationStart:
      return "kIPConfigurationStart";
    case LinkStatisticsTrigger::kConnected:
      return "kConnected";
    case LinkStatisticsTrigger::kDHCPRenewOnRoam:
      return "kDHCPRenewOnRoam";
    case LinkStatisticsTrigger::kDHCPSuccess:
      return "kDHCPSuccess";
    case LinkStatisticsTrigger::kDHCPFailure:
      return "kDHCPFailure";
    case LinkStatisticsTrigger::kSlaacFinished:
      return "kSlaacFinished";
    case LinkStatisticsTrigger::kNetworkValidationStart:
      return "kNetworkValidationStart";
    case LinkStatisticsTrigger::kNetworkValidationSuccess:
      return "kNetworkValidationSuccess";
    case LinkStatisticsTrigger::kNetworkValidationFailure:
      return "kNetworkValidationFailure";
    default:
      LOG(ERROR) << "Invalid LinkStatisticsTrigger: "
                 << static_cast<unsigned int>(trigger);
      return "Invalid";
  }
}

void WiFiLinkStatistics::Reset() {
  nl80211_link_statistics_.clear();
  rtnl_link_statistics_.clear();
}

void WiFiLinkStatistics::UpdateNl80211LinkStatistics(
    LinkStatisticsTrigger trigger, const KeyValueStore& link_statistics) {
  // nl80211 station information for WiFi link diagnosis
  if (trigger == LinkStatisticsTrigger::kUnknown) {
    return;
  }

  StationStats stats = ConvertNl80211StaInfo(link_statistics);
  // If the trigger is an end network event, erase the link statistics of its
  // start network event and print the difference to the log if necessary.
  if (IsEndNetworkEvent(trigger)) {
    for (auto it = nl80211_link_statistics_.begin();
         it != nl80211_link_statistics_.end(); it++) {
      if (!DoesEndMatchStartEvent(it->trigger, trigger)) {
        continue;
      }
      if (ShouldPrintWiFiLinkStatistics(trigger)) {
        auto diff_stats =
            Nl80211LinkStatisticsDiff(it->nl80211_link_stats, stats);
        LOG(INFO) << "Network event related to NL80211 link statistics: "
                  << LinkStatisticsTriggerToString(it->trigger) << " -> "
                  << LinkStatisticsTriggerToString(trigger)
                  << "; the NL80211 link statistics delta for the last "
                  << std::to_string(
                         (base::Time::Now() - it->timestamp).InSeconds())
                  << " seconds is "
                  << Nl80211LinkStatisticsToString(diff_stats);
      }
      nl80211_link_statistics_.erase(it);
      break;
    }
  } else {
    // The trigger is a start network event, append this snapshot of link
    // statistics.
    nl80211_link_statistics_.emplace_back(trigger, stats);
    // Add an extra nl80211 link statistics because kIPConfigurationStart
    // corresponds to the start of the initial DHCP lease acquisition by dhcpcd
    // and to the start of IPv6 SLAAC in the kernel.
    if (trigger == LinkStatisticsTrigger::kIPConfigurationStart) {
      nl80211_link_statistics_.emplace_back(trigger, stats);
    }
  }
}

void WiFiLinkStatistics::UpdateRtnlLinkStatistics(
    LinkStatisticsTrigger trigger, const old_rtnl_link_stats64& stats) {
  if (trigger == LinkStatisticsTrigger::kUnknown) {
    return;
  }
  // If the trigger is an end network event, erase the link statistics of its
  // start network event and print the difference to the log if necessary.
  if (IsEndNetworkEvent(trigger)) {
    for (auto it = rtnl_link_statistics_.begin();
         it != rtnl_link_statistics_.end(); it++) {
      if (!DoesEndMatchStartEvent(it->trigger, trigger)) {
        continue;
      }
      if (ShouldPrintWiFiLinkStatistics(trigger)) {
        auto diff_stats = RtnlLinkStatisticsDiff(it->rtnl_link_stats, stats);
        LOG(INFO) << "Network event related to RTNL link statistics: "
                  << LinkStatisticsTriggerToString(it->trigger) << " -> "
                  << LinkStatisticsTriggerToString(trigger)
                  << "; the RTNL link statistics delta for the last "
                  << std::to_string(
                         (base::Time::Now() - it->timestamp).InSeconds())
                  << " seconds is " << RtnlLinkStatisticsToString(diff_stats);
      }
      rtnl_link_statistics_.erase(it);
      break;
    }
  } else {
    // The trigger is a start network event, append this snapshot of link
    // statistics.
    rtnl_link_statistics_.emplace_back(trigger, stats);
    // Add an extra RTNL link statistics because kIPConfigurationStart
    // corresponds to the start of the initial DHCP lease acquisition by dhcpcd
    // and to the start of IPv6 SLAAC in the kernel.
    if (trigger == LinkStatisticsTrigger::kIPConfigurationStart) {
      rtnl_link_statistics_.emplace_back(trigger, stats);
    }
  }
}

}  // namespace shill
