// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dns-proxy/controller.h"

#include <sys/capability.h>
#include <sys/prctl.h>
#include <sysexits.h>

#include <set>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/process/launch.h>
#include <base/task/single_thread_task_runner.h>
#include <base/time/time.h>
#include <chromeos/patchpanel/message_dispatcher.h>
#include <chromeos/scoped_minijail.h>
#include <shill/dbus-constants.h>

#include "dns-proxy/ipc.pb.h"
#include "dns-proxy/proxy.h"

namespace dns_proxy {
namespace {

constexpr base::TimeDelta kSubprocessRestartDelay = base::Milliseconds(900);
constexpr base::TimeDelta kSubprocessMaxWaitTime = base::Seconds(3);
constexpr base::TimeDelta kSubprocessWaitSleepTime = base::Milliseconds(100);
constexpr char kSeccompPolicyPath[] =
    "/usr/share/policy/dns-proxy-seccomp.policy";
constexpr char kResolvConfRunPath[] = "/run/dns-proxy/resolv.conf";
constexpr char kDnsProxySystem[] = "dns-proxy-system";
constexpr char kDnsProxyUser[] = "dns-proxy-user";

// Loops until all child processes are stopped or there is an error. This
// function is safe to call even if |pids| contains an already stopped children
// as long as waitpid is not previously called for the pid.
bool WaitForChildren(std::set<pid_t> pids) {
  base::TimeTicks deadline = base::TimeTicks::Now() + kSubprocessMaxWaitTime;
  while (base::TimeTicks::Now() < deadline) {
    int status;
    pid_t pid = HANDLE_EINTR(waitpid(0, &status, WNOHANG));
    if (pid == -1) {
      if (errno == ECHILD) {
        return true;
      }
      PLOG(ERROR) << "Unable to find child processes";
      return false;
    }
    if (pid == 0) {
      base::PlatformThread::Sleep(kSubprocessWaitSleepTime);
      continue;
    }

    // Log child process exit status.
    if (WIFEXITED(status)) {
      LOG(INFO) << "Process " << pid << " exited with status "
                << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      LOG(INFO) << "Process " << pid << " killed by signal " << WTERMSIG(status)
                << (WCOREDUMP(status) ? " (core dumped)" : "");
    } else {
      LOG(WARNING) << "Unknown exit status " << status << " for process "
                   << pid;
    }

    // Wait until all child processes exit.
    pids.erase(pid);
    if (pids.empty()) {
      return true;
    }
  }
  LOG(WARNING) << "Reached maximum wait time before all child processes exit";
  return false;
}

}  // namespace

Controller::Controller(const std::string& progname, const std::string& vmodule)
    : progname_(progname), vmodule_(vmodule), resolv_conf_(new ResolvConf()) {}

// This ctor is only used for testing.
Controller::Controller(std::unique_ptr<ResolvConf> resolv_conf)
    : resolv_conf_(std::move(resolv_conf)) {}

int Controller::OnInit() {
  LOG(INFO) << "Starting DNS Proxy service";

  // Set run path for resolv.conf.
  resolv_conf_->set_path(base::FilePath(kResolvConfRunPath));

  // Preserve CAP_NET_BIND_SERVICE so the child processes have the capability.
  // Without the ambient set, file capabilities need to be used.
  if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_NET_BIND_SERVICE, 0, 0) !=
      0) {
    metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                Metrics::ProcessEvent::kCapNetBindServiceError);
    PLOG(FATAL) << "Failed to add CAP_NET_BIND_SERVICE to the ambient set";
  }

  // Preserve CAP_SETUID so the child process have the capability to change to
  // either "dns-proxy-user" or "dns-proxy-system".
  if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, CAP_SETUID, 0, 0) != 0) {
    PLOG(FATAL) << "Failed to add CAP_SETUID to the ambient set";
  }

  // Handle subprocess lifecycle.
  process_reaper_.Register(this);

  /// Run after Daemon::OnInit()
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&Controller::Setup, weak_factory_.GetWeakPtr()));
  return DBusDaemon::OnInit();
}

void Controller::OnShutdown(int* code) {
  LOG(INFO) << "Stopping DNS Proxy service (" << *code << ")";
  std::set<pid_t> pids = {};
  for (const auto& p : proxies_) {
    pids.emplace(p.pid);
    Kill(p);
  }
  if (!WaitForChildren(pids)) {
    LOG(WARNING) << "Failed to wait for all child processes to stop";
  } else {
    LOG(INFO) << "Stopped all child processes properly";
  }
  is_shutdown_ = true;
}

void Controller::Setup() {
  features_ = ChromeFeaturesServiceClient::New(bus_);
  if (features_) {
    features_->IsDNSProxyEnabled(base::BindOnce(&Controller::OnFeatureEnabled,
                                                weak_factory_.GetWeakPtr()));
    features_->IsRootNsDNSProxyEnabled(base::BindOnce(
        &Controller::OnRootNsEnabled, weak_factory_.GetWeakPtr()));
  } else {
    LOG(ERROR) << "Failed to initialize Chrome features client - "
               << "service will be enabled by default";
    service_enabled_.emplace(true);
    LOG(ERROR) << "Failed to initialize Chrome features client - "
               << "service will be enabled inside a network namespace";
    root_ns_enabled_.emplace(false);
  }

  patchpanel_ = patchpanel::Client::New(bus_);
  if (!patchpanel_) {
    metrics_.RecordProcessEvent(
        Metrics::ProcessType::kController,
        Metrics::ProcessEvent::kPatchpanelNotInitialized);
    LOG(ERROR) << "Failed to initialize patchpanel client";
    QuitWithExitCode(EX_UNAVAILABLE);
    return;
  }
  patchpanel_->RegisterOnAvailableCallback(base::BindOnce(
      &Controller::OnPatchpanelReady, weak_factory_.GetWeakPtr()));
  patchpanel_->RegisterProcessChangedCallback(base::BindRepeating(
      &Controller::OnPatchpanelReset, weak_factory_.GetWeakPtr()));

  shill_.reset(new shill::Client(bus_));
  shill_->RegisterProcessChangedHandler(base::BindRepeating(
      &Controller::OnShillReset, weak_factory_.GetWeakPtr()));
  shill_->RegisterOnAvailableCallback(
      base::BindOnce(&Controller::OnShillReady, weak_factory_.GetWeakPtr()));

  RunProxy(Proxy::Type::kSystem);
  RunProxy(Proxy::Type::kDefault);
}

void Controller::OnFeatureEnabled(std::optional<bool> enabled) {
  // Avoid starting child processes when the controller is shut down.
  if (is_shutdown_) {
    return;
  }
  if (!enabled.has_value()) {
    LOG(ERROR) << "Failed to read feature flag - "
               << "service will be enabled by default";
    service_enabled_.emplace(true);
  } else {
    service_enabled_.emplace(enabled.value());
    LOG(INFO) << "Service "
              << (service_enabled_.value() ? "enabled" : "disabled")
              << " by feature flag";
  }
}

void Controller::OnRootNsEnabled(std::optional<bool> enabled) {
  // Avoid starting child processes when the controller is shut down.
  if (is_shutdown_) {
    return;
  }
  if (!enabled.has_value()) {
    LOG(ERROR) << "Failed to read feature flag - "
               << "service will be enabled inside a network namespace";
    root_ns_enabled_.emplace(true);
  } else {
    root_ns_enabled_.emplace(enabled.value());
    LOG(INFO) << "Service is running on "
              << (root_ns_enabled_.value() ? "root namespace"
                                           : "network namespace");
  }
}

void Controller::OnPatchpanelReady(bool success) {
  if (!success) {
    metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                Metrics::ProcessEvent::kPatchpanelNotReady);
    LOG(ERROR) << "Failed to connect to patchpanel";
    QuitWithExitCode(EX_UNAVAILABLE);
    return;
  }
  patchpanel_->RegisterVirtualDeviceEventHandler(base::BindRepeating(
      &Controller::OnVirtualDeviceChanged, weak_factory_.GetWeakPtr()));

  // Process the current set of patchpanel devices and launch any required
  // proxy processes.
  for (const auto& d : patchpanel_->GetDevices()) {
    VirtualDeviceAdded(d);
  }
}

void Controller::OnPatchpanelReset(bool reset) {
  if (reset) {
    LOG(WARNING) << "Patchpanel has been reset";
    return;
  }

  // If patchpanel crashes, the proxies will be restarted, so just create a new
  // client and continue on.
  metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                              Metrics::ProcessEvent::kPatchpanelShutdown);
  LOG(ERROR) << "Patchpanel has been shutdown - reconnecting...";
}

void Controller::OnShillReady(bool success) {
  shill_ready_ = success;
  if (!shill_ready_) {
    metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                Metrics::ProcessEvent::kShillNotReady);
    LOG(DFATAL) << "Failed to connect to shill";
    return;
  }

  shill_->RegisterDefaultDeviceChangedHandler(base::BindRepeating(
      &Controller::OnDefaultDeviceChanged, weak_factory_.GetWeakPtr()));
}

void Controller::OnShillReset(bool reset) {
  if (reset) {
    LOG(WARNING) << "Shill has been reset";
    return;
  }

  LOG(WARNING) << "Shill has been shutdown";
  shill_ready_ = false;
  // Listen for it to come back.
  shill_->RegisterOnAvailableCallback(
      base::BindOnce(&Controller::OnShillReady, weak_factory_.GetWeakPtr()));
}

void Controller::RunProxy(Proxy::Type type, const std::string& ifname) {
  if (!service_enabled_.has_value() || !root_ns_enabled_.has_value()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Controller::RunProxy,
                                  weak_factory_.GetWeakPtr(), type, ifname));
    return;
  }
  if (!service_enabled_.value()) {
    return;
  }

  ProxyProc proc(type, ifname);
  const auto& it = restarts_.find(proc);
  if (it != restarts_.end() && !it->second.is_valid()) {
    LOG(ERROR) << "Not running blocked proxy " << proc;
    return;
  }

  if (proxies_.find(proc) != proxies_.end()) {
    return;
  }

  // Create FDs to communicate to the proxy.
  int control[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, control) != 0) {
    PLOG(ERROR) << "Failed to start proxy. socketpair failed";
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&Controller::RunProxy,
                                  weak_factory_.GetWeakPtr(), type, ifname));
    return;
  }
  base::ScopedFD controller_fd(control[0]);
  base::ScopedFD proxy_fd(control[1]);

  ScopedMinijail jail(minijail_new());
  minijail_no_new_privs(jail.get());
  minijail_use_seccomp_filter(jail.get());
  minijail_parse_seccomp_filters(jail.get(), kSeccompPolicyPath);
  minijail_forward_signals(jail.get());
  minijail_reset_signal_mask(jail.get());
  minijail_reset_signal_handlers(jail.get());
  minijail_run_as_init(jail.get());
  minijail_preserve_fd(jail.get(), proxy_fd.get(), proxy_fd.get());
  minijail_close_open_fds(jail.get());

  if (root_ns_enabled_.value()) {
    // DNS proxy uses SO_BINDTODEVICE to bind to a specific interface for
    // sending DNS queries. Switch users to honor always-on VPN related routing
    // and setup.
    if (type == Proxy::Type::kDefault) {
      minijail_change_user(jail.get(), kDnsProxyUser);
    } else {
      minijail_change_user(jail.get(), kDnsProxySystem);
    }
    // Required since we don't have the caps to wipe supplementary groups.
    minijail_keep_supplementary_gids(jail.get());
  } else {
    minijail_namespace_net(jail.get());
  }

  std::vector<char*> argv;
  const std::string flag_t = "--t=" + std::string(Proxy::TypeToString(type));
  argv.push_back(const_cast<char*>(progname_.c_str()));
  argv.push_back(const_cast<char*>(flag_t.c_str()));
  std::string flag_i = "--i=";
  if (!ifname.empty()) {
    flag_i += ifname;
    argv.push_back(const_cast<char*>(flag_i.c_str()));
  }
  std::string flag_fd = "--fd=" + std::to_string(proxy_fd.get());
  argv.push_back(const_cast<char*>(flag_fd.c_str()));
  std::string flag_vmodule = "--vmodule=" + vmodule_;
  argv.push_back(const_cast<char*>(flag_vmodule.c_str()));
  std::string flag_root_ns = "--root_ns";
  if (root_ns_enabled_.value()) {
    argv.push_back(const_cast<char*>(flag_root_ns.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid;
  if (minijail_run_pid(jail.get(), argv[0], argv.data(), &pid) != 0) {
    metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                Metrics::ProcessEvent::kProxyLaunchFailure);
    LOG(DFATAL) << "Failed to launch process for proxy " << proc;
    return;
  }
  proc.pid = pid;
  metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                              Metrics::ProcessEvent::kProxyLaunchSuccess);
  LOG(INFO) << "Launched process for proxy " << proc;

  if (!process_reaper_.WatchForChild(
          FROM_HERE, pid,
          base::BindOnce(&Controller::OnProxyExit, weak_factory_.GetWeakPtr(),
                         pid))) {
    LOG(ERROR) << "Failed to watch process for proxy " << proc
               << " - did it crash after launch?";
    return;
  }

  // Sets up communication with the proxy process.
  auto msg_dispatcher =
      std::make_unique<patchpanel::MessageDispatcher<SubprocessMessage>>(
          std::move(controller_fd));
  msg_dispatcher->RegisterFailureHandler(base::BindRepeating(
      &Controller::OnMessageFailure, weak_factory_.GetWeakPtr(), proc));
  msg_dispatcher->RegisterMessageHandler(base::BindRepeating(
      &Controller::OnMessage, weak_factory_.GetWeakPtr(), proc));
  msg_dispatchers_.emplace(proc, std::move(msg_dispatcher));

  proxies_.emplace(proc);
}

void Controller::OnMessageFailure(const ProxyProc& proc) {
  KillProxy(proc.opts.type, proc.opts.ifname, /*forget=*/false);
}

void Controller::OnMessage(const ProxyProc& proc,
                           const SubprocessMessage& msg) {
  if (!msg.has_proxy_message()) {
    LOG(ERROR) << "Unexpected message type";
    return;
  }

  const ProxyMessage& proxy_msg = msg.proxy_message();
  switch (proxy_msg.type()) {
    case ProxyMessage::SET_ADDRS:
      resolv_conf_->SetDNSProxyAddresses(std::vector<std::string>(
          proxy_msg.addrs().begin(), proxy_msg.addrs().end()));
      break;
    case ProxyMessage::CLEAR_ADDRS:
      resolv_conf_->SetDNSProxyAddresses({});
      break;
    default:
      NOTREACHED_IN_MIGRATION();
  }
}

void Controller::KillProxy(Proxy::Type type,
                           const std::string& ifname,
                           bool forget) {
  auto it = proxies_.find(ProxyProc(type, ifname));
  if (it == proxies_.end()) {
    return;
  }
  Kill(*it, forget);
  if (!forget) {
    return;
  }
  proxies_.erase(it);
  restarts_.erase(*it);
}

void Controller::Kill(const ProxyProc& proc, bool forget) {
  if (forget) {
    process_reaper_.ForgetChild(proc.pid);
  }
  // Send SHUT_DOWN message to the proxy process.
  auto it = msg_dispatchers_.find(proc);
  if (it != msg_dispatchers_.end()) {
    ControllerMessage controller_msg;
    controller_msg.set_type(ControllerMessage::SHUT_DOWN);
    SubprocessMessage msg;
    *msg.mutable_controller_message() = controller_msg;
    it->second->SendMessage(msg);
  }
  EvalProxyExit(proc);
}

void Controller::OnProxyExit(pid_t pid, const siginfo_t& siginfo) {
  process_reaper_.ForgetChild(pid);

  // There will only ever be a handful of entries in this map so a linear scan
  // will be trivial.
  ProxyProc proc;
  bool found = false;
  for (auto it = proxies_.begin(); it != proxies_.end(); ++it) {
    if (it->pid == pid) {
      proc = *it;
      proxies_.erase(it);
      found = true;
      break;
    }
  }
  if (!found) {
    metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                Metrics::ProcessEvent::kProxyMissing);
    LOG(ERROR) << "Unexpected process (" << pid << ") exit signal received";
    return;
  }

  EvalProxyExit(proc);

  switch (siginfo.si_code) {
    case CLD_EXITED:
    case CLD_DUMPED:
    case CLD_KILLED:
    case CLD_TRAPPED:
      metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                  Metrics::ProcessEvent::kProxyKilled);

      LOG(ERROR) << "Process for proxy [" << proc
                 << " was unexpectedly killed (" << siginfo.si_code << ":"
                 << siginfo.si_status << ") - "
                 << (RestartProxy(proc) ? "attempting to restart"
                                        : "restart attempts exceeded");
      break;

    case CLD_STOPPED:
      metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                  Metrics::ProcessEvent::kProxyStopped);
      LOG(WARNING) << "Process for proxy " << proc
                   << " was unexpectedly stopped";
      break;

    case CLD_CONTINUED:
      metrics_.RecordProcessEvent(Metrics::ProcessType::kController,
                                  Metrics::ProcessEvent::kProxyContinued);
      LOG(WARNING) << "Process for proxy " << proc << " has continued";
      break;

    default:
      NOTREACHED_IN_MIGRATION();
  }
}

bool Controller::RestartProxy(const ProxyProc& proc) {
  auto it = restarts_.find(proc);
  if (it == restarts_.end()) {
    // First time the process has been restarted.
    restarts_.emplace(proc, ProxyRestarts());
  } else if (!it->second.try_next()) {
    return false;
  }

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&Controller::RunProxy, weak_factory_.GetWeakPtr(),
                     proc.opts.type, proc.opts.ifname),
      kSubprocessRestartDelay);
  return true;
}

void Controller::EvalProxyExit(const ProxyProc& proc) {
  // Clean up communication with the proxy process.
  msg_dispatchers_.erase(proc);

  if (proc.opts.type != Proxy::Type::kSystem) {
    return;
  }

  // Ensure the system proxy address is cleared from shill.
  if (!shill_ready_) {
    LOG(WARNING) << "Cannot clear shill dns-property for " << proc
                 << " - shill is not connected";
    return;
  }

  shill_->GetManagerProxy()->ClearDNSProxyAddresses(nullptr /* error */);
  resolv_conf_->SetDNSProxyAddresses({});
}

void Controller::OnVirtualDeviceChanged(
    patchpanel::Client::VirtualDeviceEvent event,
    const patchpanel::Client::VirtualDevice& device) {
  switch (event) {
    case patchpanel::Client::VirtualDeviceEvent::kAdded:
      VirtualDeviceAdded(device);
      break;
    case patchpanel::Client::VirtualDeviceEvent::kRemoved:
      VirtualDeviceRemoved(device);
      break;
    default:
      NOTREACHED_IN_MIGRATION();
  }
}

void Controller::VirtualDeviceAdded(
    const patchpanel::Client::VirtualDevice& device) {
  if (patchpanel::Client::IsArcGuest(device.guest_type)) {
    RunProxy(Proxy::Type::kARC, device.phys_ifname);
  }
}

void Controller::VirtualDeviceRemoved(
    const patchpanel::Client::VirtualDevice& device) {
  if (patchpanel::Client::IsArcGuest(device.guest_type)) {
    KillProxy(Proxy::Type::kARC, device.phys_ifname);
  }
}

void Controller::OnDefaultDeviceChanged(
    const shill::Client::Device* const device) {
  // Default service is either not ready yet or has just disconnected.
  if (!device) {
    return;
  }

  // Use pointer to avoid unnecessary copies.
  auto* network_config = &device->network_config;

  // Special case for VPN without nameserver. Fallback to default physical
  // network's nameserver(s).
  if (device->type == shill::Client::Device::Type::kVPN &&
      device->network_config.dns_servers.empty()) {
    auto dd = shill_->DefaultDevice(/*exclude_vpn=*/true);
    if (!dd) {
      LOG(ERROR) << "No default non-VPN device found";
      return;
    }
    network_config = &dd->network_config;
  }

  resolv_conf_->SetDNSFromLists(network_config->dns_servers,
                                network_config->dns_search_domains);
}

}  // namespace dns_proxy
