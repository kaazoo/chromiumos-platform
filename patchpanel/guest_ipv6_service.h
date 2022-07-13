// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_GUEST_IPV6_SERVICE_H_
#define PATCHPANEL_GUEST_IPV6_SERVICE_H_

#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>

#include "patchpanel/datapath.h"
#include "patchpanel/helper_process.h"
#include "patchpanel/ipc.pb.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class GuestIPv6Service {
 public:
  enum class ForwardMethod { kMethodUnknown, kMethodNDProxy, kMethodRAServer };

  GuestIPv6Service(HelperProcess* nd_proxy,
                   Datapath* datapath,
                   ShillClient* shill_client);
  GuestIPv6Service(const GuestIPv6Service&) = delete;
  GuestIPv6Service& operator=(const GuestIPv6Service&) = delete;
  virtual ~GuestIPv6Service() = default;

  void Start();

  void StartForwarding(const std::string& ifname_uplink,
                       const std::string& ifname_downlink,
                       bool downlink_is_tethering = false);

  void StopForwarding(const std::string& ifname_uplink,
                      const std::string& ifname_downlink);

  void StopUplink(const std::string& ifname_uplink);

  // For local hotspot there is no uplink. We need to first start the RA
  // server on the tethering link with the provided prefix info.
  // StartForwarding() is still expected to be called among this link and
  // other downlinks later to propagate this private prefix to those
  // downlinks and to enable NA/NS forwarding.
  void StartLocalHotspot(const std::string& ifname_hotspot_link,
                         const std::string& prefix,
                         const std::vector<std::string>& rdnss,
                         const std::vector<std::string>& dnssl);

  void StopLocalHotspot(const std::string& ifname_hotspot_link);

  // Allow manually set a uplink to use NDProxy or RA server for test
  // purpose. This will be exposed by Manager through dbus for tast.
  void SetForwardMethod(const std::string& ifname_uplink, ForwardMethod method);

 private:
  // Callback from NDProxy telling us to add a new IPv6 route to guest or IPv6
  // address to guest-facing interface.
  void OnNDProxyMessage(const NDProxyMessage& msg);

  // IPv6 neighbor discovery forwarder process handler. Owned by Manager.
  HelperProcess* nd_proxy_;
  // Routing and iptables controller service. Owned by Manager.
  Datapath* datapath_;
  // Shill Dbus client. Owned by Manager.
  ShillClient* shill_client_;

  base::WeakPtrFactory<GuestIPv6Service> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_GUEST_IPV6_SERVICE_H_
