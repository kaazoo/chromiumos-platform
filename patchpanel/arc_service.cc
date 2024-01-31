// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/arc_service.h"

#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include <optional>
#include <string_view>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>
#include <brillo/key_value_store.h>
#include <chromeos/constants/vm_tools.h>
#include <metrics/metrics_library.h>
#include <net-base/ipv4_address.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

#include "patchpanel/adb_proxy.h"
#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/mac_address_generator.h"
#include "patchpanel/metrics.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/net_util.h"
#include "patchpanel/patchpanel_daemon.h"
#include "patchpanel/proto_utils.h"
#include "patchpanel/scoped_ns.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/vm_concierge_client.h"

namespace patchpanel {
namespace {
// UID of Android root, relative to the host pid namespace.
const int32_t kAndroidRootUid = 655360;
// Allocate 5 subnets for physical interfaces.
constexpr uint32_t kConfigPoolSize = 5;
constexpr uint32_t kInvalidId = 0;
constexpr char kArcNetnsName[] = "arc_netns";
constexpr char kArcVmIfnamePrefix[] = "eth";

void RecordEvent(MetricsLibraryInterface* metrics, ArcServiceUmaEvent event) {
  metrics->SendEnumToUMA(kArcServiceUmaEventMetrics, event);
}

std::optional<ArcService::ArcDevice::Technology> TranslateTechnologyType(
    ShillClient::Device::Type type) {
  using ShillType = ShillClient::Device::Type;
  switch (type) {
    case ShillType::kCellular:
      return ArcService::ArcDevice::Technology::kCellular;
    case ShillType::kWifi:
      return ArcService::ArcDevice::Technology::kWiFi;
    case ShillType::kEthernet:
      // fallthrough.
    case ShillType::kEthernetEap:
      return ArcService::ArcDevice::Technology::kEthernet;
    case ShillType::kVPN:
      // fallthrough.
    case ShillType::kGuestInterface:
      // fallthrough.
    case ShillType::kLoopback:
      // fallthrough.
    case ShillType::kPPP:
      // fallthrough.
    case ShillType::kTunnel:
      // fallthrough.
    case ShillType::kUnknown:
      return std::nullopt;
  }
}

bool IsAdbAllowed(ShillClient::Device::Type type) {
  static const std::set<ShillClient::Device::Type> adb_allowed_types{
      ShillClient::Device::Type::kEthernet,
      ShillClient::Device::Type::kEthernetEap,
      ShillClient::Device::Type::kWifi,
  };
  return adb_allowed_types.find(type) != adb_allowed_types.end();
}

bool KernelVersion(int* major, int* minor) {
  struct utsname u;
  if (uname(&u) != 0) {
    PLOG(ERROR) << "uname failed";
    *major = *minor = 0;
    return false;
  }
  int unused;
  if (sscanf(u.release, "%d.%d.%d", major, minor, &unused) != 3) {
    LOG(ERROR) << "unexpected release string: " << u.release;
    *major = *minor = 0;
    return false;
  }
  return true;
}

// Makes Android root the owner of /sys/class/ + |path|. |pid| is the ARC
// container pid.
bool SetSysfsOwnerToAndroidRoot(pid_t pid, const std::string& path) {
  auto ns = ScopedNS::EnterMountNS(pid);
  if (!ns) {
    LOG(ERROR) << "Cannot enter mnt namespace for pid " << pid;
    return false;
  }

  const std::string sysfs_path = "/sys/class/" + path;
  if (chown(sysfs_path.c_str(), kAndroidRootUid, kAndroidRootUid) == -1) {
    PLOG(ERROR) << "Failed to change ownership of " + sysfs_path;
    return false;
  }

  return true;
}

bool OneTimeContainerSetup(Datapath& datapath, pid_t pid) {
  static bool done = false;
  if (done)
    return true;

  bool success = true;

  // Load networking modules needed by Android that are not compiled in the
  // kernel. Android does not allow auto-loading of kernel modules.
  // Expected for all kernels.
  if (!datapath.ModprobeAll({
          // The netfilter modules needed by netd for iptables commands.
          "ip6table_filter",
          "ip6t_ipv6header",
          "ip6t_REJECT",
          // The ipsec modules for AH and ESP encryption for ipv6.
          "ah6",
          "esp6",
      })) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
    success = false;
  }
  // The xfrm modules needed for Android's ipsec APIs on kernels < 5.4.
  int major, minor;
  if (KernelVersion(&major, &minor) &&
      (major < 5 || (major == 5 && minor < 4)) &&
      !datapath.ModprobeAll({
          "xfrm4_mode_transport",
          "xfrm4_mode_tunnel",
          "xfrm6_mode_transport",
          "xfrm6_mode_tunnel",
      })) {
    LOG(ERROR) << "One or more required kernel modules failed to load."
               << " Some Android functionality may be broken.";
    success = false;
  }

  // Additional modules optional for CTS compliance but required for some
  // Android features.
  if (!datapath.ModprobeAll({
          // This module is not available in kernels < 3.18
          "nf_reject_ipv6",
          // These modules are needed for supporting Chrome traffic on Android
          // VPN which uses Android's NAT feature. Android NAT sets up
          // iptables
          // rules that use these conntrack modules for FTP/TFTP.
          "nf_nat_ftp",
          "nf_nat_tftp",
          // The tun module is needed by the Android 464xlat clatd process.
          "tun",
      })) {
    LOG(WARNING) << "One or more optional kernel modules failed to load.";
    success = false;
  }

  // This is only needed for CTS (b/27932574).
  if (!SetSysfsOwnerToAndroidRoot(pid, "xt_idletimer")) {
    success = false;
  }

  done = true;
  return success;
}

std::string PrefixIfname(std::string_view prefix, std::string_view ifname) {
  std::string n;
  n.append(prefix);
  n.append(ifname);
  if (n.length() >= IFNAMSIZ) {
    n.resize(IFNAMSIZ - 1);
    // Best effort attempt to preserve the interface number, assuming it's the
    // last char in the name.
    n.back() = ifname.back();
  }
  return n;
}
}  // namespace

bool ArcService::IsVM(ArcType type) {
  switch (type) {
    case ArcType::kContainer:
      return false;
    case ArcType::kVMHotplug:
      return true;
    case ArcType::kVMStatic:
      return true;
  }
}

ArcService::ArcConfig::ArcConfig(const MacAddress& mac_addr,
                                 std::unique_ptr<Subnet> ipv4_subnet)
    : mac_addr_(mac_addr), ipv4_subnet_(std::move(ipv4_subnet)) {}

ArcService::ArcDevice::ArcDevice(
    ArcType type,
    std::optional<ArcDevice::Technology> technology,
    std::optional<std::string_view> shill_device_ifname,
    std::string_view arc_device_ifname,
    const MacAddress& arc_device_mac_address,
    const ArcConfig& arc_config,
    std::string_view bridge_ifname,
    std::string_view guest_device_ifname)
    : type_(type),
      technology_(technology),
      shill_device_ifname_(shill_device_ifname),
      arc_device_ifname_(arc_device_ifname),
      arc_device_mac_address_(arc_device_mac_address),
      arc_ipv4_subnet_(arc_config.ipv4_subnet()),
      arc_ipv4_address_(arc_config.arc_ipv4_address()),
      bridge_ipv4_address_(arc_config.bridge_ipv4_address()),
      bridge_ifname_(bridge_ifname),
      guest_device_ifname_(guest_device_ifname) {}

ArcService::ArcDevice::~ArcDevice() {}

void ArcService::ArcDevice::ConvertToProto(NetworkDevice* output) const {
  // By convention, |phys_ifname| is set to "arc0" for the "arc0" device used
  // for VPN forwarding.
  output->set_phys_ifname(shill_device_ifname().value_or(kArc0Ifname));
  output->set_ifname(bridge_ifname());
  output->set_guest_ifname(guest_device_ifname());
  output->set_ipv4_addr(arc_ipv4_address().address().ToInAddr().s_addr);
  output->set_host_ipv4_addr(bridge_ipv4_address().address().ToInAddr().s_addr);
  if (IsVM(type())) {
    output->set_guest_type(NetworkDevice::ARCVM);
  } else {
    output->set_guest_type(NetworkDevice::ARC);
  }
  if (technology().has_value()) {
    switch (technology().value()) {
      case ArcDevice::Technology::kCellular:
        output->set_technology_type(NetworkDevice::CELLULAR);
        break;
      case ArcDevice::Technology::kWiFi:
        output->set_technology_type(NetworkDevice::WIFI);
        break;
      case ArcDevice::Technology::kEthernet:
        output->set_technology_type(NetworkDevice::ETHERNET);
        break;
    }
  }
  FillSubnetProto(arc_ipv4_subnet(), output->mutable_ipv4_subnet());
}

ArcService::StaticGuestIfManager::StaticGuestIfManager(
    const std::vector<std::string>& host_ifnames) {
  int eth_idx = 0;
  // Inside ARCVM, interface names follow the pattern eth%d (starting from 0)
  // following the order of the host tap interfaces.
  for (const auto& host_ifname : host_ifnames) {
    guest_if_names_.try_emplace(host_ifname,
                                kArcVmIfnamePrefix + std::to_string(eth_idx++));
  }
}

std::optional<std::string> ArcService::StaticGuestIfManager::AddInterface(
    const std::string& host_ifname) {
  LOG(ERROR) << "Interface cannot be added to a static VM.";
  return std::nullopt;
}

bool ArcService::StaticGuestIfManager::RemoveInterface(
    const std::string& host_ifname) {
  LOG(ERROR) << "Interface cannot be removed from a static VM.";
  return false;
}

std::optional<std::string> ArcService::StaticGuestIfManager::GetGuestIfName(
    const std::string& host_ifname) const {
  auto itr = guest_if_names_.find(host_ifname);
  if (itr == guest_if_names_.end()) {
    return std::nullopt;
  } else {
    return itr->second;
  }
}

std::vector<std::string> ArcService::StaticGuestIfManager::GetStaticTapDevices()
    const {
  std::vector<std::string> guest_if_name_vec;
  for (const auto& map_itr : guest_if_names_) {
    guest_if_name_vec.push_back(map_itr.first);
  }
  return guest_if_name_vec;
}

ArcService::HotplugGuestIfManager::HotplugGuestIfManager(
    std::unique_ptr<VmConciergeClient> vm_concierge_client,
    const std::string& arc0_tap_ifname,
    uint32_t cid)
    : arc0_tap_ifname_(arc0_tap_ifname), cid_(cid) {
  client_ = std::move(vm_concierge_client);
  // eth0 is always occupied by arc0 device, and excluded from hotplug.
  guest_if_idx_bitset_.set(0);
  client_->RegisterVm(cid);
}

void ArcService::HotplugGuestIfManager::HotplugCallback(
    std::string tap_ifname, std::optional<uint32_t> bus_num) {
  if (!bus_num.has_value()) {
    LOG(ERROR) << "Hotplug host tap " << tap_ifname
               << " to guest failed: concierge error.";
    return;
  }
  // Valid PCI Bus indices are 0-255 inclusive.
  if (*bus_num > UINT8_MAX) {
    LOG(ERROR) << "Hotplug host tap " << tap_ifname
               << " to guest failed: invalid bus number " << *bus_num;
    return;
  }
  const uint8_t bus_num_uint8 = uint8_t(*bus_num);
  const auto emplace_result =
      guest_buses_.try_emplace(std::move(tap_ifname), bus_num_uint8);
  if (emplace_result.second) {
    LOG(INFO) << "Hotplug host tap " << tap_ifname
              << " to guest succeeded, guest bus: " << bus_num_uint8;
  } else {
    LOG(ERROR) << "Hotplug host tap " << tap_ifname
               << " failed: device was already reported inserted at bus "
               << guest_buses_.at(tap_ifname) << ", but replaced by "
               << bus_num_uint8;
    emplace_result.first->second = bus_num_uint8;
  }
}

void ArcService::HotplugGuestIfManager::RemoveCallback(
    const std::string& tap_ifname, bool success) {
  if (!success) {
    LOG(ERROR) << "Remove host tap" << tap_ifname
               << " failed: concierge error.";
    return;
  }
  if (guest_buses_.erase(tap_ifname) == 0) {
    LOG(WARNING) << tap_ifname << " is already removed";
  }
}

std::optional<std::string> ArcService::HotplugGuestIfManager::AddInterface(
    const std::string& tap_ifname) {
  if (guest_if_idx_.find(tap_ifname) != guest_if_idx_.end()) {
    LOG(ERROR) << "Hotplug host tap " << tap_ifname
               << " failed: tap is already attached to the guest.";
    return std::nullopt;
  }
  if (!client_->AttachTapDevice(
          cid_, tap_ifname,
          base::BindOnce(&HotplugGuestIfManager::HotplugCallback,
                         base::Unretained(this), tap_ifname))) {
    LOG(ERROR) << "Hotplug host tap " << tap_ifname
               << " failed: cannot make DBus request to concierge.";
    return std::nullopt;
  }
  // The index of the ethernet device is the lowest integer not currently used.
  for (size_t i = 0; i < guest_if_idx_bitset_.size(); i++) {
    if (!guest_if_idx_bitset_.test(i)) {
      guest_if_idx_bitset_.set(i);
      guest_if_idx_.emplace(tap_ifname, i);
      return kArcVmIfnamePrefix + std::to_string(i);
    }
  }
  LOG(ERROR) << "Hotplug host tap " << tap_ifname
             << " failed: all possible network indices are already taken.";
  return std::nullopt;
}

bool ArcService::HotplugGuestIfManager::RemoveInterface(
    const std::string& tap_ifname) {
  auto idx_itr = guest_if_idx_.find(tap_ifname);
  if (idx_itr == guest_if_idx_.end()) {
    LOG(ERROR) << "Remove network interface failed: " << tap_ifname
               << " is not found on guest";
    return false;
  }
  auto bus_itr = guest_buses_.find(tap_ifname);
  if (bus_itr == guest_buses_.end()) {
    LOG(ERROR) << "Remove network interface failed: " << tap_ifname
               << " hotplug failed";
    return false;
  }
  if (!client_->DetachTapDevice(
          cid_, bus_itr->second,
          base::BindOnce(&HotplugGuestIfManager::RemoveCallback,
                         base::Unretained(this), tap_ifname))) {
    LOG(ERROR) << "Remove network interface failed";
    return false;
  }
  guest_if_idx_bitset_.reset(idx_itr->second);
  guest_if_idx_.erase(idx_itr);
  return true;
}

std::optional<std::string> ArcService::HotplugGuestIfManager::GetGuestIfName(
    const std::string& tap_ifname) const {
  auto itr = guest_if_idx_.find(tap_ifname);
  if (itr == guest_if_idx_.end()) {
    return std::nullopt;
  } else {
    return kArcVmIfnamePrefix + std::to_string(itr->second);
  }
}

std::vector<std::string>
ArcService::HotplugGuestIfManager::GetStaticTapDevices() const {
  // For ARCVM with hotplug support, only the arc0 device is always attached.
  return {arc0_tap_ifname_};
}

ArcService::ArcService(ArcType arc_type,
                       Datapath* datapath,
                       AddressManager* addr_mgr,
                       ForwardingService* forwarding_service,
                       MetricsLibraryInterface* metrics,
                       DbusClientNotifier* dbus_client_notifier)
    : arc_type_(arc_type),
      datapath_(datapath),
      addr_mgr_(addr_mgr),
      forwarding_service_(forwarding_service),
      metrics_(metrics),
      dbus_client_notifier_(dbus_client_notifier),
      id_(kInvalidId) {
  DCHECK(datapath_);
  DCHECK(addr_mgr_);
  DCHECK(forwarding_service_);
  AllocateArc0Config();
  AllocateAddressConfigs();
}

ArcService::~ArcService() {
  if (IsStarted()) {
    Stop(id_);
  }
}

bool ArcService::IsStarted() const {
  return id_ != kInvalidId;
}

// Creates the ARC management Device used for VPN forwarding, ADB-over-TCP.
void ArcService::AllocateArc0Config() {
  auto ipv4_subnet =
      addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArc0);
  if (!ipv4_subnet) {
    LOG(ERROR) << __func__ << ": No subnet available";
    return;
  }
  uint32_t subnet_index = (IsVM(arc_type_)) ? 1 : kAnySubnetIndex;
  arc0_config_ = std::make_unique<ArcConfig>(
      addr_mgr_->GenerateMacAddress(subnet_index), std::move(ipv4_subnet));
  all_configs_.push_back(arc0_config_.get());
}

void ArcService::AllocateAddressConfigs() {
  // The first usable subnet is the "other" ARC Device subnet.
  // As a temporary workaround, for ARCVM, allocate fixed MAC addresses.
  uint32_t mac_addr_index = 2;
  for (int config_index = 0; config_index < kConfigPoolSize; config_index++) {
    auto ipv4_subnet =
        addr_mgr_->AllocateIPv4Subnet(AddressManager::GuestType::kArcNet);
    if (!ipv4_subnet) {
      LOG(ERROR) << __func__ << ": Subnet already in use or unavailable";
      continue;
    }
    MacAddress mac_addr = (arc_type_ == ArcType::kVMStatic)
                              ? addr_mgr_->GenerateMacAddress(mac_addr_index++)
                              : addr_mgr_->GenerateMacAddress();
    const auto& config = available_configs_.emplace_back(
        std::make_unique<ArcConfig>(mac_addr, std::move(ipv4_subnet)));
    all_configs_.push_back(config.get());
  }
}

void ArcService::RefreshMacAddressesInConfigs() {
  for (auto* config : all_configs_) {
    config->set_mac_addr(addr_mgr_->GenerateMacAddress());
  }
}

std::unique_ptr<ArcService::ArcConfig> ArcService::AcquireConfig() {
  if (available_configs_.empty()) {
    LOG(ERROR) << "Cannot make virtual Device: No more addresses available.";
    return nullptr;
  }

  std::unique_ptr<ArcConfig> config;
  config = std::move(available_configs_.back());
  available_configs_.pop_back();
  return config;
}

void ArcService::ReleaseConfig(std::unique_ptr<ArcConfig> config) {
  available_configs_.emplace_back(std::move(config));
}

bool ArcService::Start(uint32_t id) {
  RecordEvent(metrics_, ArcServiceUmaEvent::kStart);

  if (IsStarted()) {
    RecordEvent(metrics_, ArcServiceUmaEvent::kStartWithoutStop);
    LOG(WARNING) << "Already running - did something crash?"
                 << " Stopping and restarting...";
    Stop(id_);
  }

  std::string arc0_device_ifname;
  if (!arc0_config_) {
    LOG(ERROR) << "arc0 config not allocated";
    return false;
  }
  switch (arc_type_) {
    case ArcType::kContainer: {
      pid_t pid = static_cast<int>(id);
      if (pid < 0) {
        LOG(ERROR) << "Invalid ARC container pid " << pid;
        return false;
      }
      if (!OneTimeContainerSetup(*datapath_, pid)) {
        RecordEvent(metrics_, ArcServiceUmaEvent::kOneTimeContainerSetupError);
        LOG(ERROR) << "One time container setup failed";
      }
      if (!datapath_->NetnsAttachName(kArcNetnsName, pid)) {
        LOG(ERROR) << "Failed to attach name " << kArcNetnsName << " to pid "
                   << pid;
        return false;
      }
      // b/208240700: Refresh MAC address in AddressConfigs every time ARC
      // starts to ensure ARC container has different MAC after optout and
      // reopt-in.
      // TODO(b/185881882): this should be safe to remove after b/185881882.
      RefreshMacAddressesInConfigs();

      arc0_device_ifname = kVethArc0Ifname;
      break;
    }
    case ArcType::kVMHotplug: {
      // Allocate TAP device for arc0 device.
      auto tap = datapath_->AddTunTap(/*name=*/"", arc0_config_->mac_addr(),
                                      /*ipv4_cidr=*/std::nullopt,
                                      vm_tools::kCrosVmUser, DeviceMode::kTap);
      if (tap.empty()) {
        LOG(ERROR) << "Failed to create TAP device for arc0";
        break;
      }
      arc0_config_->set_tap_ifname(tap);
      arc0_device_ifname = tap;
      guest_if_manager_ = std::make_unique<HotplugGuestIfManager>(
          VmConciergeClientImpl::CreateClientWithNewBus(), arc0_device_ifname,
          id);
      break;
    }
    case ArcType::kVMStatic: {
      // Allocate TAP devices for all configs.
      std::vector<std::string> tap_ifnames;
      for (auto* config : all_configs_) {
        // Tap device name is autogenerated. IPv4 is configured on the bridge.
        auto tap =
            datapath_->AddTunTap(/*name=*/"", config->mac_addr(),
                                 /*ipv4_cidr=*/std::nullopt,
                                 vm_tools::kCrosVmUser, DeviceMode::kTap);
        if (tap.empty()) {
          LOG(ERROR) << "Failed to create TAP device";
          continue;
        }

        config->set_tap_ifname(tap);
        tap_ifnames.push_back(std::move(tap));
      }
      guest_if_manager_ = std::make_unique<StaticGuestIfManager>(tap_ifnames);
      arc0_device_ifname = arc0_config_->tap_ifname();
    }
  }

  id_ = id;

  // The "arc0" virtual device is either attached on demand to host VPNs or is
  // used to forward host traffic into an Android VPN. Therefore, |shill_device|
  // is not meaningful for the "arc0" virtual device and is undefined.
  arc0_device_ = ArcDevice(arc_type_, std::nullopt, std::nullopt,
                           arc0_device_ifname, arc0_config_->mac_addr(),
                           *arc0_config_, kArcbr0Ifname, kArc0Ifname);

  LOG(INFO) << "Starting ARC management Device " << *arc0_device_;
  StartArcDeviceDatapath(*arc0_device_);

  // Start already known shill <-> ARC mapped devices.
  for (const auto& [_, shill_device] : shill_devices_)
    AddDevice(shill_device);

  // Enable conntrack helpers needed for processing through SNAT the IPv4 GRE
  // packets sent by Android PPTP client (b/172214190).
  // TODO(b/252749921) Find alternative for chromeos-6.1+ kernels.
  if (!datapath_->SetConntrackHelpers(true)) {
    // Do not consider this error fatal for ARC datapath setup (b/252749921).
    LOG(ERROR) << "Failed to enable conntrack helpers";
  }

  RecordEvent(metrics_, ArcServiceUmaEvent::kStartSuccess);
  return true;
}

void ArcService::Stop(uint32_t id) {
  RecordEvent(metrics_, ArcServiceUmaEvent::kStop);
  if (!IsStarted()) {
    RecordEvent(metrics_, ArcServiceUmaEvent::kStopBeforeStart);
    LOG(ERROR) << "ArcService was not running";
    return;
  }

  // After the ARC container has stopped, the pid is not known anymore.
  // The stop message for ARCVM may be sent after a new VM is started. Only
  // stop if the CID matched the latest started ARCVM CID.
  if (IsVM(arc_type_) && id_ != id) {
    LOG(WARNING) << "Mismatched ARCVM CIDs " << id_ << " != " << id;
    return;
  }

  if (!datapath_->SetConntrackHelpers(false))
    LOG(ERROR) << "Failed to disable conntrack helpers";

  // Remove all ARC Devices associated with a shill Device.
  // Make a copy of |shill_devices_| to avoid invalidating any iterator over
  // |shill_devices_| while removing device from it and resetting it afterwards.
  auto shill_devices = shill_devices_;
  for (const auto& [_, shill_device] : shill_devices) {
    RemoveDevice(shill_device);
  }
  shill_devices_ = shill_devices;

  StopArcDeviceDatapath(*arc0_device_);
  LOG(INFO) << "Stopped ARC management Device " << *arc0_device_;
  arc0_device_ = std::nullopt;

  if (IsVM(arc_type_)) {
    guest_if_manager_.reset();
    for (auto* config : all_configs_) {
      if (config->tap_ifname().empty()) {
        continue;
      }
      datapath_->RemoveInterface(config->tap_ifname());
      config->set_tap_ifname("");
    }
  } else {
    // Free the network namespace name attached to the ARC container.
    if (!datapath_->NetnsDeleteName(kArcNetnsName)) {
      LOG(ERROR) << "Failed to delete netns name " << kArcNetnsName;
    }
  }

  id_ = kInvalidId;
  is_arc_interactive_ = true;
  is_android_wifi_multicast_lock_held_ = false;
  RecordEvent(metrics_, ArcServiceUmaEvent::kStopSuccess);
}

void ArcService::AddDevice(const ShillClient::Device& shill_device) {
  shill_devices_[shill_device.shill_device_interface_property] = shill_device;
  if (!IsStarted())
    return;

  if (shill_device.ifname.empty())
    return;

  RecordEvent(metrics_, ArcServiceUmaEvent::kAddDevice);

  if (devices_.find(shill_device.ifname) != devices_.end()) {
    LOG(DFATAL) << "Attemping to add already tracked shill device "
                << shill_device;
    return;
  }

  // TODO(b:323291863): Fix config leak when AddDevice fails.
  auto config = AcquireConfig();
  if (!config) {
    LOG(ERROR) << "Cannot acquire an ARC IPv4 config for shill device "
               << shill_device;
    return;
  }

  if (arc_type_ == ArcType::kVMHotplug) {
    auto tap_ifname = datapath_->AddTunTap(
        /*name=*/"", config->mac_addr(), /*ipv4_cidr=*/std::nullopt,
        vm_tools::kCrosVmUser, DeviceMode::kTap);
    if (tap_ifname.empty()) {
      LOG(ERROR) << "Failed to create tap device for shill device "
                 << shill_device;
      return;
    }
    if (guest_if_manager_->AddInterface(tap_ifname)->empty()) {
      LOG(ERROR) << "Failed to hotplug tap device " << tap_ifname
                 << " to guest for shill device " << shill_device;
      return;
    }
    config->set_tap_ifname(tap_ifname);
  }
  // The interface name visible inside ARC depends on the type of ARC
  // environment:
  //  - ARC container: the veth interface created inside ARC has the same name
  //  as the shill Device that this ARC virtual device is attached to.
  //  b/273741099: For Cellular multiplexed interfaces, the name of the shill
  //  Device is used such that the rest of the ARC stack does not need to be
  //  aware of Cellular multiplexing.
  //  - ARCVM: |guest_if_manager_| tracks the name of guest interfaces.
  std::string arc_device_ifname;
  std::string guest_ifname;
  if (IsVM(arc_type_)) {
    arc_device_ifname = config->tap_ifname();
    if (arc_device_ifname.empty()) {
      LOG(ERROR) << "No TAP device for " << shill_device;
      return;
    }
    const auto guest_ifname_opt =
        guest_if_manager_->GetGuestIfName(config->tap_ifname());
    if (!guest_ifname_opt.has_value()) {
      LOG(ERROR) << "No guest device for " << shill_device;
      return;
    }
    guest_ifname = *guest_ifname_opt;
  } else {  // arc_type_ == kContainer
    arc_device_ifname = ArcVethHostName(shill_device);
    guest_ifname = shill_device.shill_device_interface_property;
  }

  const std::optional<ArcDevice::Technology> technology =
      TranslateTechnologyType(shill_device.type);
  if (!technology.has_value()) {
    LOG(ERROR) << "Shill device technology type " << shill_device.type
               << " is invalid for ArcDevice.";
    return;
  }

  auto arc_device_it = devices_.try_emplace(
      shill_device.ifname, arc_type_, technology.value(),
      shill_device.shill_device_interface_property, arc_device_ifname,
      config->mac_addr(), *config, ArcBridgeName(shill_device), guest_ifname);

  LOG(INFO) << "Starting ARC Device " << arc_device_it.first->second;
  StartArcDeviceDatapath(arc_device_it.first->second);
  // Only start forwarding multicast traffic if ARC is in an interactive state.
  // In addition, on WiFi the Android WiFi multicast lock must also be held.
  bool forward_multicast =
      is_arc_interactive_ &&
      (shill_device.type != ShillClient::Device::Type::kWifi ||
       is_android_wifi_multicast_lock_held_);
  forwarding_service_->StartForwarding(
      shill_device, arc_device_it.first->second.bridge_ifname(),
      {.ipv6 = true, .multicast = forward_multicast});
  auto signal_device = std::make_unique<NetworkDevice>();
  arc_device_it.first->second.ConvertToProto(signal_device.get());
  dbus_client_notifier_->OnNetworkDeviceChanged(
      std::move(signal_device), NetworkDeviceChangedSignal::DEVICE_ADDED);
  assigned_configs_.emplace(shill_device.ifname, std::move(config));
  RecordEvent(metrics_, ArcServiceUmaEvent::kAddDeviceSuccess);
}

void ArcService::RemoveDevice(const ShillClient::Device& shill_device) {
  if (IsStarted()) {
    const auto it = devices_.find(shill_device.ifname);
    if (it == devices_.end()) {
      LOG(WARNING) << "Unknown shill Device " << shill_device;
    } else {
      const auto& arc_device = it->second;
      LOG(INFO) << "Removing ARC Device " << arc_device;
      if (arc_type_ == ArcType::kVMHotplug) {
        guest_if_manager_->RemoveInterface(arc_device.arc_device_ifname());
      }
      auto signal_device = std::make_unique<NetworkDevice>();
      arc_device.ConvertToProto(signal_device.get());
      dbus_client_notifier_->OnNetworkDeviceChanged(
          std::move(signal_device), NetworkDeviceChangedSignal::DEVICE_REMOVED);
      forwarding_service_->StopForwarding(shill_device,
                                          arc_device.bridge_ifname());
      StopArcDeviceDatapath(arc_device);
      auto config_it = assigned_configs_.find(shill_device.ifname);
      if (config_it == assigned_configs_.end()) {
        LOG(ERROR) << "No IPv4 configuration found for ARC Device "
                   << arc_device;
      } else {
        if (arc_type_ == ArcType::kVMHotplug) {
          datapath_->RemoveTunTap(config_it->second->tap_ifname(),
                                  DeviceMode::kTap);
          config_it->second->set_tap_ifname("");
        }
        ReleaseConfig(std::move(config_it->second));
        assigned_configs_.erase(config_it);
      }
      devices_.erase(it);
    }
  }
  shill_devices_.erase(shill_device.shill_device_interface_property);
}

void ArcService::UpdateDeviceIPConfig(const ShillClient::Device& shill_device) {
  auto shill_device_it =
      shill_devices_.find(shill_device.shill_device_interface_property);
  if (shill_device_it == shill_devices_.end()) {
    LOG(WARNING) << "Unknown shill Device " << shill_device;
    return;
  }
  shill_device_it->second = shill_device;
}

std::optional<net_base::IPv4Address> ArcService::GetArc0IPv4Address() const {
  if (!arc0_config_) {
    return std::nullopt;
  }
  return arc0_config_->arc_ipv4_address().address();
}

std::vector<std::string> ArcService::GetStaticTapDevices() const {
  if (IsVM(arc_type_)) {
    return guest_if_manager_->GetStaticTapDevices();
  }
  return {};
}

std::vector<const ArcService::ArcDevice*> ArcService::GetDevices() const {
  std::vector<const ArcDevice*> devices;
  for (const auto& [_, dev] : devices_) {
    devices.push_back(&dev);
  }
  return devices;
}

// static
std::string ArcService::ArcVethHostName(const ShillClient::Device& device) {
  return PrefixIfname("veth", device.shill_device_interface_property);
}

// static
std::string ArcService::ArcBridgeName(const ShillClient::Device& device) {
  return PrefixIfname("arc_", device.shill_device_interface_property);
}

std::ostream& operator<<(std::ostream& stream,
                         const ArcService::ArcDevice& arc_device) {
  stream << "{ type: " << arc_device.type()
         << ", arc_device_ifname: " << arc_device.arc_device_ifname()
         << ", arc_ipv4_addr: " << arc_device.arc_ipv4_address()
         << ", arc_device_mac_addr: "
         << MacAddressToString(arc_device.arc_device_mac_address())
         << ", bridge_ifname: " << arc_device.bridge_ifname()
         << ", bridge_ipv4_addr: " << arc_device.bridge_ipv4_address()
         << ", guest_device_ifname: " << arc_device.guest_device_ifname();
  if (arc_device.shill_device_ifname()) {
    stream << ", shill_ifname: " << *arc_device.shill_device_ifname();
  }
  return stream << '}';
}

std::ostream& operator<<(std::ostream& stream, ArcService::ArcType arc_type) {
  switch (arc_type) {
    case ArcService::ArcType::kContainer:
      return stream << "ARC Container";
    case ArcService::ArcType::kVMStatic:
      return stream << "ARCVM";
    case ArcService::ArcType::kVMHotplug:
      return stream << "ARCVM with hotplug support";
  }
}

void ArcService::StartArcDeviceDatapath(
    const ArcService::ArcDevice& arc_device) {
  // Only create the host virtual interface and guest virtual interface for
  // the container. The TAP devices are currently always created statically
  // ahead of time.
  if (arc_type_ == ArcType::kContainer) {
    pid_t pid = static_cast<int>(id_);
    if (pid < 0) {
      LOG(ERROR) << __func__ << "(" << arc_device
                 << "): Invalid ARC container pid " << pid;
      return;
    }
    // ARC requires multicast capability at all times. This is tested as part of
    // CTS and CDD.
    if (!datapath_->ConnectVethPair(
            pid, kArcNetnsName, arc_device.arc_device_ifname(),
            arc_device.guest_device_ifname(),
            arc_device.arc_device_mac_address(), arc_device.arc_ipv4_address(),
            /*remote_ipv6_cidr=*/std::nullopt,
            /*remote_multicast_flag=*/true)) {
      LOG(ERROR) << __func__ << "(" << arc_device
                 << "): Cannot create virtual ethernet pair";
      return;
    }
    // Allow netd to write to /sys/class/net/arc0/mtu (b/175571457).
    if (!SetSysfsOwnerToAndroidRoot(
            pid, "net/" + arc_device.guest_device_ifname() + "/mtu")) {
      RecordEvent(metrics_, ArcServiceUmaEvent::kSetVethMtuError);
    }
  }

  // Create the associated bridge and link the host virtual device to the
  // bridge.
  if (!datapath_->AddBridge(arc_device.bridge_ifname(),
                            arc_device.bridge_ipv4_address())) {
    LOG(ERROR) << __func__ << "(" << arc_device << "): Failed to setup bridge";
    return;
  }

  if (!datapath_->AddToBridge(arc_device.bridge_ifname(),
                              arc_device.arc_device_ifname())) {
    LOG(ERROR) << __func__ << "(" << arc_device
               << "): Failed to link bridge and ARC virtual interface";
    return;
  }

  if (!arc_device.shill_device_ifname()) {
    return;
  }

  // Only setup additional iptables rules for ARC Devices bound to a shill
  // Device. The iptables rules for arc0 are configured only when a VPN
  // connection exists and are triggered directly from Manager when the default
  // logical network switches to a VPN.
  const auto shill_device_it =
      shill_devices_.find(*arc_device.shill_device_ifname());
  if (shill_device_it == shill_devices_.end()) {
    LOG(ERROR) << __func__ << "(" << arc_device
               << "): Failed to find shill Device";
    return;
  }

  datapath_->StartRoutingDevice(
      shill_device_it->second, arc_device.bridge_ifname(), TrafficSource::kArc);
  datapath_->AddInboundIPv4DNAT(AutoDNATTarget::kArc, shill_device_it->second,
                                arc_device.arc_ipv4_address().address());
  if (IsAdbAllowed(shill_device_it->second.type) &&
      !datapath_->AddAdbPortAccessRule(shill_device_it->second.ifname)) {
    LOG(ERROR) << __func__ << "(" << arc_device
               << "): Failed to add ADB port access rule";
  }
}

void ArcService::StopArcDeviceDatapath(
    const ArcService::ArcDevice& arc_device) {
  if (arc_device.shill_device_ifname()) {
    const auto shill_device_it =
        shill_devices_.find(*arc_device.shill_device_ifname());
    if (shill_device_it == shill_devices_.end()) {
      LOG(ERROR) << __func__ << "(" << arc_device
                 << "): Failed to find shill Device";
    } else {
      if (IsAdbAllowed(shill_device_it->second.type)) {
        datapath_->DeleteAdbPortAccessRule(shill_device_it->second.ifname);
      }
      datapath_->RemoveInboundIPv4DNAT(AutoDNATTarget::kArc,
                                       shill_device_it->second,
                                       arc_device.arc_ipv4_address().address());
      datapath_->StopRoutingDevice(arc_device.bridge_ifname(),
                                   TrafficSource::kArc);
    }
  }
  datapath_->RemoveBridge(arc_device.bridge_ifname());

  // Only destroy the host virtual interface for the container. ARCVM TAP
  // devices are removed separately when ARC stops.
  if (arc_type_ == ArcType::kContainer) {
    datapath_->RemoveInterface(arc_device.arc_device_ifname());
  }
}

void ArcService::NotifyAndroidWifiMulticastLockChange(bool is_held) {
  if (!IsStarted()) {
    return;
  }

  // When multicast lock status changes from not held to held or the other
  // way, decide whether to enable or disable multicast forwarder for ARC.
  if (is_android_wifi_multicast_lock_held_ == is_held) {
    return;
  }
  is_android_wifi_multicast_lock_held_ = is_held;

  // If arc is not interactive, multicast lock held status does not
  // affect multicast traffic.
  if (!is_arc_interactive_) {
    return;
  }

  // Only start/stop forwarding when multicast allowed status changes to avoid
  // start/stop forwarding multiple times, also wifi multicast lock should
  // only affect multicast traffic on wireless device.
  for (const auto& [shill_device_ifname, arc_device] : devices_) {
    const auto shill_device_it = shill_devices_.find(shill_device_ifname);
    if (shill_device_it == shill_devices_.end()) {
      LOG(ERROR) << __func__
                 << ": no upstream shill Device found for ARC Device "
                 << arc_device;
      continue;
    }
    if (shill_device_it->second.type != ShillClient::Device::Type::kWifi) {
      continue;
    }
    if (is_android_wifi_multicast_lock_held_) {
      forwarding_service_->StartForwarding(
          shill_device_it->second, arc_device.bridge_ifname(),
          ForwardingService::ForwardingSet{.multicast = true});
    } else {
      forwarding_service_->StopForwarding(
          shill_device_it->second, arc_device.bridge_ifname(),
          ForwardingService::ForwardingSet{.multicast = true});
    }
  }
}

void ArcService::NotifyAndroidInteractiveState(bool is_interactive) {
  if (!IsStarted()) {
    return;
  }

  if (is_arc_interactive_ == is_interactive) {
    return;
  }
  is_arc_interactive_ = is_interactive;

  // If ARC power state has changed to interactive, enable all
  // interfaces that are not WiFi interface, and only enable WiFi interfaces
  // when WiFi multicast lock is held.
  // If ARC power state has changed to non-interactive, disable all
  // interfaces that are not WiFi interface, and only disable WiFi
  // interfaces when they were in enabled state (multicast lock held).
  for (const auto& [shill_device_ifname, arc_device] : devices_) {
    const auto shill_device_it = shill_devices_.find(shill_device_ifname);
    if (shill_device_it == shill_devices_.end()) {
      LOG(ERROR) << __func__
                 << ": no upstream shill Device found for ARC Device "
                 << arc_device;
      continue;
    }
    if (shill_device_it->second.type == ShillClient::Device::Type::kWifi &&
        !is_android_wifi_multicast_lock_held_) {
      continue;
    }
    if (is_arc_interactive_) {
      forwarding_service_->StartForwarding(
          shill_device_it->second, arc_device.bridge_ifname(),
          ForwardingService::ForwardingSet{.multicast = true});
    } else {
      forwarding_service_->StopForwarding(
          shill_device_it->second, arc_device.bridge_ifname(),
          ForwardingService::ForwardingSet{.multicast = true});
    }
  }
}

bool ArcService::IsWiFiMulticastForwardingRunning() {
  // Check multicast forwarding conditions for WiFi. This implies ARC is
  // running.
  if (!is_arc_interactive_ || !is_android_wifi_multicast_lock_held_) {
    return false;
  }
  // Ensure there is also an active WiFi Device;
  for (const auto& [_, shill_dev] : shill_devices_) {
    if (shill_dev.type == ShillClient::Device::Type::kWifi) {
      return true;
    }
  }
  return false;
}

}  // namespace patchpanel
