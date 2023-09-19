// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_WIFI_P2P_DEVICE_H_
#define SHILL_WIFI_P2P_DEVICE_H_

#include <memory>
#include <optional>
#include <string>

#include "shill/wifi/local_device.h"
#include "shill/wifi/p2p_service.h"

namespace shill {

class Manager;

class P2PDevice : public LocalDevice {
 public:
  enum class P2PDeviceState {
    // Common states for all roles.
    kUninitialized,  // P2PDevice instance created, but no interface is created
                     // in kernel
    kReady,  // Any prerequisite steps (like connect to the primary interface,
             // get up to date phy info) are done on the device and can start
             // the P2P process

    // P2P client states.
    kClientAssociating,  // P2P client is connecting to a group
    kClientConfiguring,  // P2P client has joined an L2 P2P group and is setting
                         // up L3 connectivity
    kClientConnected,    // P2P client has joined a group and L3 link has been
                         // established
    kClientDisconnecting,  // P2P client is disconnecting from a group

    // P2P GO states.
    kGOStarting,     // P2P GO is creating a group
    kGOConfiguring,  // P2P GO has created an L2 P2P group and is setting up L3
                     // network
    kGOActive,       // P2P GO has created a group and can accept connections
    kGOStopping,     // P2P GO is destroying a group
  };

  // Constructor function
  P2PDevice(Manager* manager,
            LocalDevice::IfaceType iface_type,
            const std::string& primary_link_name,
            uint32_t phy_index,
            uint32_t shill_id,
            LocalDevice::EventCallback callback);

  P2PDevice(const P2PDevice&) = delete;
  P2PDevice& operator=(const P2PDevice&) = delete;

  ~P2PDevice() override;

  static const char* P2PDeviceStateName(P2PDeviceState state);

  // Get properties of group managed by this device (GO only).
  mockable KeyValueStore GetGroupInfo() const;

  // Get properties of client connection managed by this device (GC only).
  mockable KeyValueStore GetClientInfo() const;

  // P2PDevice start routine. Override the base class Start.
  bool Start() override;

  // P2PDevice stop routine. Override the base class Stop.
  bool Stop() override;

  // Return the configured service on this device.
  LocalService* GetService() const override { return service_.get(); }

  // Creates a P2P group with the current device as the group owner
  // using the setting from |service|. Functionality is stubbed.
  mockable bool CreateGroup(std::unique_ptr<P2PService> service);

  // Starts a P2P connection with a device |peer_address| with the
  // specified configuration in |service|. Functionality is stubbed.
  mockable bool Connect(std::unique_ptr<P2PService> service);

  // Removes the current P2P group. Functionality is stubbed.
  bool RemoveGroup();

  // Disconnect a P2P connection with a device |peer_address|.
  // Functionality is stubbed.
  bool Disconnect();

  // Set device link_name;
  void SetLinkName(std::string link_name) { link_name_ = link_name; }

  // Set P2PDeviceState.
  void SetState(P2PDeviceState state);

  // Get shill_id_.
  uint32_t shill_id() const { return shill_id_; }

 private:
  friend class P2PDeviceTest;
  FRIEND_TEST(P2PDeviceTest, DeviceOnOff);

  // Set service_ to |service|.
  bool SetService(std::unique_ptr<P2PService> service);

  // Delete service_.
  void DeleteService();

  // Returns true if the device is in an active GO state.
  bool InGOState() const;

  // Returns true if the device is in an active Client state.
  bool InClientState() const;

  // Primary interface link name.
  std::string primary_link_name_;

  // Uniquely identifies this device relative to all other P2P devices in Shill.
  uint32_t shill_id_;
  // P2P device state as listed in enum P2PDeviceState.
  P2PDeviceState state_;
  // P2P service configured on this device.
  std::unique_ptr<P2PService> service_;
};

}  // namespace shill

#endif  // SHILL_WIFI_P2P_DEVICE_H_
