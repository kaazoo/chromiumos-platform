// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_ARC_VPN_DRIVER_H_
#define SHILL_VPN_ARC_VPN_DRIVER_H_

#include <memory>

#include <chromeos/net-base/network_config.h>
#include <chromeos/net-base/process_manager.h>
#include <gtest/gtest_prod.h>

#include "shill/error.h"
#include "shill/vpn/vpn_driver.h"

namespace shill {

class ArcVpnDriver : public VPNDriver {
 public:
  ArcVpnDriver(Manager* manager, net_base::ProcessManager* process_manager);
  ArcVpnDriver(const ArcVpnDriver&) = delete;
  ArcVpnDriver& operator=(const ArcVpnDriver&) = delete;

  ~ArcVpnDriver() override = default;

  base::TimeDelta ConnectAsync(EventHandler* handler) override;
  void Disconnect() override;
  void OnConnectTimeout() override;
  std::unique_ptr<net_base::NetworkConfig> GetNetworkConfig() const override;

 private:
  static const Property kProperties[];

  // Called in ConnectAsync() by PostTask(), to make sure |handler| is valid.
  void InvokeEventHandler(EventHandler* handler);

  base::WeakPtrFactory<ArcVpnDriver> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_VPN_ARC_VPN_DRIVER_H_
