// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_PROTO_UTILS_H_
#define PATCHPANEL_PROTO_UTILS_H_

#include <map>
#include <string>

#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/datapath.h"
#include "patchpanel/device.h"
#include "patchpanel/subnet.h"

namespace patchpanel {

// Fills a protobuf TerminaVmStartupResponse object with the given
// |termina_device| Device.
void FillTerminaAllocationProto(const Device& termina_device,
                                TerminaVmStartupResponse* output);

// Fills a protobuf ParallelsVmStartupResponse object with the given
// |parallels_device| Device.
void FillParallelsAllocationProto(const Device& parallels_device,
                                  ParallelsVmStartupResponse* output);

// Fills a protobuf NetworkDevice object with the given |virtual_device| Device.
void FillDeviceProto(const Device& virtual_device, NetworkDevice* output);

// Fills a protobuf IPv4Subnet object with the IPv4CIDR.
void FillSubnetProto(const net_base::IPv4CIDR& cidr, IPv4Subnet* output);
void FillSubnetProto(const Subnet& virtual_subnet, IPv4Subnet* output);

void FillDeviceDnsProxyProto(
    const Device& virtual_device,
    NetworkDevice* output,
    const std::map<std::string, net_base::IPv4Address>& ipv4_addrs,
    const std::map<std::string, net_base::IPv6Address>& ipv6_addrs);

void FillDownstreamNetworkProto(
    const DownstreamNetworkInfo& downstream_network_info,
    DownstreamNetwork* output);

void FillNetworkClientInfoProto(const DownstreamClientInfo& network_client_info,
                                NetworkClientInfo* output);

}  // namespace patchpanel
#endif  // PATCHPANEL_PROTO_UTILS_H_
