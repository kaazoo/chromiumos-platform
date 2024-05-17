// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_CONTROLLER_H_
#define SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <net-base/process_manager.h>

#include "dhcpcd/dbus-proxies.h"
#include "shill/network/dhcpcd_controller_interface.h"
#include "shill/network/legacy_dhcpcd/legacy_dhcpcd_listener.h"
#include "shill/technology.h"

namespace shill {

// The controller for the legacy dhcpcd 7.2.5 with ChromeOS patches.
// It communiates with the dhcpcd process through the dhcpcd D-Bus API.
class LegacyDHCPCDController : public DHCPCDControllerInterface {
 public:
  LegacyDHCPCDController(
      std::string_view interface,
      DHCPCDControllerInterface::EventHandler* handler,
      std::unique_ptr<org::chromium::dhcpcdProxy> dhcpcd_proxy,
      base::ScopedClosureRunner destroy_cb);
  ~LegacyDHCPCDController() override;

  // Implements DHCPCDControllerInterface.
  bool Rebind() override;
  bool Release() override;

  // Called by LegacyDHCPCDControllerFactory. Delegates the signals to
  // |handler_|.
  void OnDHCPEvent(EventReason reason, const KeyValueStore& configuration);
  void OnStatusChanged(Status status);

  // Gets the WeakPtr of this instance.
  base::WeakPtr<LegacyDHCPCDController> GetWeakPtr();

 private:
  // The dhcpcd D-Bus proxy
  std::unique_ptr<org::chromium::dhcpcdProxy> dhcpcd_proxy_;

  // The callback that will be executed when the instance is destroyed.
  base::ScopedClosureRunner destroy_cb_;

  base::WeakPtrFactory<LegacyDHCPCDController> weak_ptr_factory_{this};
};

// The factory class to create LegacyDHCPCDController. The factory tracks all
// the alive controller instances, and holds a LegacyDHCPCDListener that listens
// the D-Bus signal from the dhcpcd process. The listener delegates the received
// signal to the factory instance, then the factory delegates the signal to the
// corresponding controller.
class LegacyDHCPCDControllerFactory : public DHCPCDControllerFactoryInterface {
 public:
  LegacyDHCPCDControllerFactory(
      EventDispatcher* dispatcher,
      scoped_refptr<dbus::Bus> bus,
      net_base::ProcessManager* process_manager =
          net_base::ProcessManager::GetInstance(),
      std::unique_ptr<LegacyDHCPCDListenerFactory> listener_factory =
          std::make_unique<LegacyDHCPCDListenerFactory>());
  ~LegacyDHCPCDControllerFactory() override;

  // Implements DHCPCDControllerFactoryInterface.
  // Starts the dhcpcd process, and creates the LegacyDHCPCDController instance
  // when the listener receives the first signal from the dhcpcd process.
  bool CreateAsync(std::string_view interface,
                   Technology technology,
                   const DHCPCDControllerInterface::Options& options,
                   DHCPCDControllerInterface::EventHandler* handler,
                   CreateCB create_cb) override;

 private:
  // Stores the information for creating the controller instance, and the
  // closure that cleans up the dhcpcd process when the struct is destroyed.
  struct PendingRequest {
    std::string interface;
    DHCPCDControllerInterface::EventHandler* handler;
    CreateCB create_cb;
    base::ScopedClosureRunner clean_up_closure;
  };

  // Stores the alive controller and the closure that cleans up the dhcpcd
  // process when the struct is destroyed.
  struct AliveController {
    base::WeakPtr<LegacyDHCPCDController> controller;
    base::ScopedClosureRunner clean_up_closure;
  };

  // The callback from ProcessManager, called when the dhcpcd process is exited.
  void OnProcessExited(int pid, int exit_status);

  // The callback from LegacyDHCPCDListener.
  void OnDHCPEvent(std::string_view service_name,
                   uint32_t pid,
                   DHCPCDControllerInterface::EventReason reason_str,
                   const KeyValueStore& configuration);
  void OnStatusChanged(std::string_view service_name,
                       uint32_t pid,
                       DHCPCDControllerInterface::Status status);

  // The callback from LegacyDHCPCDController, called when the controller
  // instance is destroyed.
  void OnControllerDestroyed(int pid);

  // Creates the controller if there is a pending request and the controller is
  // yet to be created.
  void CreateControllerIfPending(std::string_view service_name, int pid);

  // Gets the alive controller by pid. Returns nullptr if the controller is not
  // found.
  LegacyDHCPCDController* GetAliveController(int pid) const;

  net_base::ProcessManager* process_manager_;
  scoped_refptr<dbus::Bus> bus_;

  // The listener that listens the D-Bus signal from the dhcpcd process.
  std::unique_ptr<LegacyDHCPCDListener> listener_;

  // The pending requests of CreateAsync() method. If |pending_request_|
  // contains a pid, then there is a running dhcpcd process with the pid.
  std::map<int /*pid*/, PendingRequest> pending_requests_;

  // The alive controllers. If |alive_controllers_| contains a pid, then there
  // is a running dhcpcd process with the pid.
  std::map<int /*pid*/, AliveController> alive_controllers_;

  base::WeakPtrFactory<LegacyDHCPCDControllerFactory> weak_ptr_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_LEGACY_DHCPCD_LEGACY_DHCPCD_CONTROLLER_H_
