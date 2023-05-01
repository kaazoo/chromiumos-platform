// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CROSTINI_SERVICE_H_
#define PATCHPANEL_CROSTINI_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/device.h"
#include "patchpanel/guest_type.h"
#include "patchpanel/ipc.h"
#include "patchpanel/routing_service.h"

namespace patchpanel {

// Crostini networking service handling address allocation, TAP device creation,
// and patchpanel Device management for Crostini VMs (Termina VMs, Plugin VMs).
// CrostiniService currently only supports one TAP device per VM instance.
class CrostiniService {
 public:
  // All types of VM supported by CrostiniService.
  enum class VMType {
    // Crostini Linux VM with a user LXD container.
    kTermina,
    // Parallel VM.
    kParallel,
  };

  static std::optional<VMType> VMTypeFromGuestType(GuestType guest_type);
  static std::optional<VMType> VMTypeFromProtoGuestType(
      NetworkDevice::GuestType guest_type);
  static TrafficSource TrafficSourceFromVMType(VMType vm_type);
  // Converts VMType to an internal IPC GuestMessage::GuestType value. This type
  // is needed for the Device::ChangeEventHandler callback.
  static GuestMessage::GuestType GuestMessageTypeFromVMType(VMType vm_type);
  // Converts VMType to an internal GuestType enum value. This type is needed
  // for allocating static IPv4 subnets and for the internal Device class.
  static GuestType GuestTypeFromVMType(VMType vm_type);

  // All pointers are required and must not be null, and are owned by the
  // caller.
  CrostiniService(AddressManager* addr_mgr,
                  Datapath* datapath,
                  Device::ChangeEventHandler device_changed_handler);
  CrostiniService(const CrostiniService&) = delete;
  CrostiniService& operator=(const CrostiniService&) = delete;

  ~CrostiniService();

  const Device* Start(uint64_t vm_id, VMType vm_type, uint32_t subnet_index);
  void Stop(uint64_t vm_id);

  // Returns a single Device pointer created for the VM with id |vm_id|.
  const Device* const GetDevice(uint64_t vm_id) const;

  // Returns a list of all tap Devices currently managed by this service.
  std::vector<const Device*> GetDevices() const;

 private:
  std::unique_ptr<Device> AddTAP(VMType vm_type, uint32_t subnet_index);

  // Checks ADB sideloading status and set it to |adb_sideloading_enabled_|.
  // This function will call itself again if ADB sideloading status is not
  // known yet. Otherwise, it will process all currently running Crostini VMs.
  void CheckAdbSideloadingStatus();

  // Start and stop ADB traffic forwarding from Crostini's TAP device
  // patchpanel's adb-proxy. |ifname| is the Crostini's TAP interface that
  // will be forwarded. These methods call permission broker DBUS APIs to port
  // forward and accept traffic.
  void StartAdbPortForwarding(const std::string& ifname);
  void StopAdbPortForwarding(const std::string& ifname);

  AddressManager* addr_mgr_;
  Datapath* datapath_;
  Device::ChangeEventHandler device_changed_handler_;

  // Mapping of VM IDs to TAP devices
  std::map<uint64_t, std::unique_ptr<Device>> taps_;

  bool adb_sideloading_enabled_;
  scoped_refptr<dbus::Bus> bus_;

  base::WeakPtrFactory<CrostiniService> weak_factory_{this};
};

std::ostream& operator<<(std::ostream& stream,
                         const CrostiniService::VMType vm_type);

}  // namespace patchpanel

#endif  // PATCHPANEL_CROSTINI_SERVICE_H_
