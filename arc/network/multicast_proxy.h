// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_MULTICAST_PROXY_H_
#define ARC_NETWORK_MULTICAST_PROXY_H_

#include <map>
#include <memory>
#include <string>

#include <brillo/daemons/daemon.h>

#include "arc/network/broadcast_forwarder.h"
#include "arc/network/message_dispatcher.h"
#include "arc/network/multicast_forwarder.h"

namespace arc_networkd {

// MulticastProxy manages multiple MulticastForwarder instances to forward
// multicast for multiple physical interfaces.
class MulticastProxy : public brillo::Daemon {
 public:
  explicit MulticastProxy(base::ScopedFD control_fd);
  virtual ~MulticastProxy() = default;

 protected:
  int OnInit() override;

  void OnParentProcessExit();
  void OnDeviceMessage(const DeviceMessage& msg);

 private:
  void Reset();

  MessageDispatcher msg_dispatcher_;
  std::map<std::string, std::unique_ptr<MulticastForwarder>> mdns_fwds_;
  std::map<std::string, std::unique_ptr<MulticastForwarder>> ssdp_fwds_;
  std::map<std::string, std::unique_ptr<BroadcastForwarder>> bcast_fwds_;

  base::WeakPtrFactory<MulticastProxy> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(MulticastProxy);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_MULTICAST_PROXY_H_
