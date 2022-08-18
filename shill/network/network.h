// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_NETWORK_H_
#define SHILL_NETWORK_NETWORK_H_

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/callback.h>
#include <base/time/time.h>

#include "shill/connection.h"
#include "shill/ipconfig.h"
#include "shill/mockable.h"
#include "shill/net/ip_address.h"
#include "shill/net/rtnl_handler.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcp_provider.h"
#include "shill/technology.h"

namespace shill {

class DeviceInfo;
class EventDispatcher;
class RoutingTable;
class Service;

// An object of Network class represents a network interface in the kernel, and
// maintains the layer 3 configuration on this interface.
// TODO(b/232177767): Currently this class is mainly a wrapper of the Connection
// class.
class Network {
 public:
  // The EventHandler is passed in in the constructor of Network to listen to
  // the events generated by Network. The object implements this interface must
  // have a longer life time that the Network object, e.g., that object can be
  // the owner of this Network object.
  class EventHandler {
   public:
    // Called every time when the network config on the connection is updated.
    // When this callback is called, the Network must be in a connected state,
    // but this signal does not always indicate a change from a non-connected
    // state to a connected state.
    virtual void OnConnectionUpdated(IPConfig* ipconfig) = 0;

    // Called when the Network becomes idle from a non-idle state (configuring
    // or connected), no matter if this state change is caused by a failure
    // (e.g., DHCP failure) or a user-initiate disconnect. |is_failure|
    // indicates this failure is triggered by a DHCP failure. Note that
    // currently this is the only failure type generated inside the Network
    // class.
    virtual void OnNetworkStopped(bool is_failure) = 0;

    // The IPConfig object lists held by this Network has changed.
    virtual void OnIPConfigsPropertyUpdated() = 0;

    // Called when a new DHCPv4 lease is obtained for this device. This is
    // called before OnConnectionUpdated() is called as a result of the lease
    // acquisition.
    virtual void OnGetDHCPLease() = 0;
    // Called when DHCPv4 fails to acquire a lease.
    virtual void OnGetDHCPFailure() = 0;
    // Called on when an IPv6 address is obtained from SLAAC. SLAAC is initiated
    // by the kernel when the link is connected and is currently not monitored
    // by shill. Derived class should implement this function to listen to this
    // event. Base class does nothing. This is called before
    // OnConnectionUpdated() is called and before captive portal detection is
    // started if IPv4 is not configured.
    virtual void OnGetSLAACAddress() = 0;

    // Called after IPv4 has been configured as a result of acquiring a new DHCP
    // lease. This is called after OnGetDHCPLease, OnIPConfigsPropertyUpdated,
    // and OnConnectionUpdated.
    virtual void OnIPv4ConfiguredWithDHCPLease() = 0;
    // Called after IPv6 has been configured as a result of acquiring an IPv6
    // address from the kernel when SLAAC completes. This is called after
    // OnGetSLAACAddress, OnIPConfigsPropertyUpdated, and OnConnectionUpdated
    // (if IPv4 is not yet configured).
    virtual void OnIPv6ConfiguredWithSLAACAddress() = 0;

    // TODO(b/232177767): Get the list of uids whose traffic should be blocked
    // on this connection. This is not a signal or callback. Put it here just to
    // avoid introducing Manager dependency on Network. Find a better solution
    // later.
    virtual std::vector<uint32_t> GetBlackholedUids() = 0;
  };

  // Options for starting a network.
  struct StartOptions {
    // Start DHCP client on this interface if |dhcp| is not empty.
    std::optional<DHCPProvider::Options> dhcp;
    // Accept router advertisements for IPv6.
    bool accept_ra = false;
  };

  // Note that |event_handler| should live longer than the created Network
  // object, so usually it should be the owner of this object.
  explicit Network(int interface_index,
                   const std::string& interface_name,
                   Technology technology,
                   bool fixed_ip_params,
                   EventHandler* event_handler,
                   ControlInterface* control_interface,
                   DeviceInfo* device_info,
                   EventDispatcher* dispatcher);
  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;
  virtual ~Network() = default;

  // Starts the network with the given |options|.
  void Start(const StartOptions& options);
  // Configures (or reconfigures) the associated Connection object with the
  // given IPConfig.
  void SetupConnection(IPConfig* ipconfig);
  // Stops the network connection. OnNetworkStopped() will be called when
  // cleaning up the network state is finished.
  void Stop();
  // Returns if the associated Connection object exist. Note that the return
  // value does not indicate any real state of the network. This function will
  // finally be removed.
  mockable bool HasConnectionObject() const;

  // Sets IPv4 properties specific to technology. Currently this is used by
  // cellular and VPN.
  void set_link_protocol_ipv4_properties(
      std::optional<IPConfig::Properties> props) {
    link_protocol_ipv4_properties_ = props;
  }

  int interface_index() const { return interface_index_; }
  std::string interface_name() const { return interface_name_; }

  // Interfaces between Service and Network.
  // Callback invoked when the static IP properties configured on the selected
  // service changed.
  mockable void OnStaticIPConfigChanged(const NetworkConfig& config);
  // Register a callback that gets called when the |current_ipconfig_| changed.
  // This should only be used by Service.
  mockable void RegisterCurrentIPConfigChangeHandler(
      base::RepeatingClosure handler);
  // Returns the IPConfig object which is used to setup the Connection of this
  // Network. Returns nullptr if there is no such IPConfig.
  mockable IPConfig* GetCurrentIPConfig() const;
  // The NetworkConfig before applying the static one. Only needed by Service.
  const NetworkConfig& saved_network_config() const {
    return saved_network_config_;
  }

  // Functions for DHCP.
  // Initiate renewal of existing DHCP lease. Return false if the renewal failed
  // immediately, or we don't have active lease now.
  bool RenewDHCPLease();
  // Destroy the lease, if any, with this |name|.
  // Called by the service during Unload() as part of the cleanup sequence.
  mockable void DestroyDHCPLease(const std::string& name);
  // Calculates the duration till a DHCP lease is due for renewal, and stores
  // this value in |result|. Returns std::nullopt if there is no upcoming DHCP
  // lease renewal, base::TimeDelta wrapped in std::optional otherwise.
  std::optional<base::TimeDelta> TimeToNextDHCPLeaseRenewal();

  // Functions for IPv6.
  void StopIPv6();
  void StartIPv6();
  // Invalidate the IPv6 config kept in shill and wait for the new config from
  // the kernel.
  void InvalidateIPv6Config();
  void set_ipv6_static_properties(const IPConfig::Properties& props) {
    ipv6_static_properties_ = props;
  }
  // Called by DeviceInfo.
  void EnableIPv6Privacy();
  // Called by DeviceInfo when the kernel adds or removes a globally-scoped
  // IPv6 address from this interface.
  mockable void OnIPv6AddressChanged(const IPAddress* address);
  // Called by DeviceInfo when the kernel receives a update for IPv6 DNS server
  // addresses from this interface.
  mockable void OnIPv6DnsServerAddressesChanged();

  // Set an IP configuration flag on the device. |family| should be "ipv6" or
  // "ipv4". |flag| should be the name of the flag to be set and |value| is
  // what this flag should be set to. Overridden by unit tests to pretend
  // writing to procfs.
  mockable bool SetIPFlag(IPAddress::Family family,
                          const std::string& flag,
                          const std::string& value);

  // Returns a WeakPtr of the Network.
  base::WeakPtr<Network> AsWeakPtr() { return weak_factory_.GetWeakPtr(); }

  // TODO(b/232177767): Wrappers for the corresponding functions in the
  // Connection class. This is a temporary solution. The caller should guarantee
  // there is a Connection object inside this object.
  mockable void SetPriority(uint32_t priority, bool is_primary_physical);
  mockable bool IsDefault() const;
  mockable void SetUseDNS(bool enable);
  void UpdateDNSServers(const std::vector<std::string>& dns_servers);
  void UpdateRoutingPolicy();
  mockable std::string GetSubnetName() const;
  bool IsIPv6() const;

  // TODO(b/232177767): Getters for access members in Connection. This is a
  // temporary solution. The caller should guarantee there is a Connection
  // object inside this object.
  mockable const std::vector<std::string>& dns_servers() const;
  mockable const IPAddress& local() const;
  const IPAddress& gateway() const;

  // TODO(b/232177767): Remove once we eliminate all Connection references in
  // shill.
  Connection* connection() const { return connection_.get(); }

  // TODO(b/232177767): This group of getters and setters are only exposed for
  // the purpose of refactor. New code outside Device should not use these.
  IPConfig* ipconfig() const { return ipconfig_.get(); }
  IPConfig* ip6config() const { return ip6config_.get(); }
  void set_dhcp_controller(std::unique_ptr<DHCPController> controller) {
    dhcp_controller_ = std::move(controller);
  }
  void set_ipconfig(std::unique_ptr<IPConfig> config) {
    ipconfig_ = std::move(config);
  }
  void set_ip6config(std::unique_ptr<IPConfig> config) {
    ip6config_ = std::move(config);
  }
  bool fixed_ip_params() const { return fixed_ip_params_; }

  // Only used in tests.
  void set_connection_for_testing(std::unique_ptr<Connection> connection) {
    connection_ = std::move(connection);
  }
  void set_fixed_ip_params_for_testing(bool val) { fixed_ip_params_ = val; }
  void set_dhcp_provider_for_testing(DHCPProvider* provider) {
    dhcp_provider_ = provider;
  }

 private:
  // TODO(b/232177767): Refactor DeviceTest to remove this dependency.
  friend class DeviceTest;

  void StopInternal(bool is_failure);

  // Functions for IPv4.
  // Triggers a reconfiguration on connection for an IPv4 config change.
  void OnIPv4ConfigUpdated();
  // Callback registered with DHCPController. Also see the comment for
  // DHCPController::UpdateCallback.
  void OnIPConfigUpdatedFromDHCP(const IPConfig::Properties& properties,
                                 bool new_lease_acquired);
  // Callback invoked on DHCP failures.
  void OnDHCPFailure();

  // Functions for IPv6.
  // Timer function for monitoring IPv6 DNS server's lifetime.
  void StartIPv6DNSServerTimer(base::TimeDelta lifetime);
  void StopIPv6DNSServerTimer();
  // Called when the lifetime for IPv6 DNS server expires.
  void IPv6DNSServerExpired();
  // Configures static IP address received from cellular bearer.
  void ConfigureStaticIPv6Address();
  // Called when IPv6 configuration changes.
  void OnIPv6ConfigUpdated();

  const int interface_index_;
  const std::string interface_name_;
  const Technology technology_;

  // If true, IP parameters should not be modified. This should not be changed
  // after a Network object is created. Make it modifiable just for unit tests.
  bool fixed_ip_params_;

  std::unique_ptr<Connection> connection_;

  std::unique_ptr<DHCPController> dhcp_controller_;
  std::unique_ptr<IPConfig> ipconfig_;
  std::unique_ptr<IPConfig> ip6config_;

  base::RepeatingClosure current_ipconfig_change_handler_;
  // If not empty, |current_ipconfig_| should points to either |ipconfig_| or
  // |ip6config_| which is used to setup the connection. GetCurrentIPConfig()
  // should be used to get this property so that its validity can be checked.
  IPConfig* current_ipconfig_ = nullptr;

  // The technology-specific IPv4 config properties. Currently only used by
  // cellular and VPN. Assume that when this field is not empty, it must have
  // valid values to set up the connection (e.g., at least address and prefix
  // len).
  std::optional<IPConfig::Properties> link_protocol_ipv4_properties_;

  // TODO(b/227563210): We currently use ip6config() for IPv6 network properties
  // from SLAAC and this separated |ipv6_static_properties_| for static
  // configurations from cellular. This is temporary and only works because we
  // always expect a SLAAC config to be available (which will not be true for
  // VPN). Will come back to rework after the Device-Network refactor. Note that
  // in the current implementation this variable will not be reset by Network
  // class itself.
  std::optional<IPConfig::Properties> ipv6_static_properties_;

  // The static NetworkConfig from the associated Service.
  NetworkConfig static_network_config_;
  // The NetworkConfig before applying a static one. This will be used for 1)
  // able to restore the config to the previous state and 2) being exposed as a
  // Service property via D-Bus.
  NetworkConfig saved_network_config_;

  // Callback to invoke when IPv6 DNS servers lifetime expired.
  base::CancelableOnceClosure ipv6_dns_server_expired_callback_;

  // Remember which flag files were previously successfully written. Only used
  // in SetIPFlag().
  std::set<std::string> written_flags_;

  EventHandler* event_handler_;

  // Other dependencies.
  ControlInterface* control_interface_;
  DeviceInfo* device_info_;
  EventDispatcher* dispatcher_;

  // Cache singleton pointers for performance and test purposes.
  DHCPProvider* dhcp_provider_;
  RoutingTable* routing_table_;
  RTNLHandler* rtnl_handler_;

  base::WeakPtrFactory<Network> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_NETWORK_NETWORK_H_
