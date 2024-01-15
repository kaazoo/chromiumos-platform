// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_monitor.h"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <net-base/network_config.h>

#include "shill/network/connection_diagnostics.h"
#include "shill/network/validation_log.h"

namespace shill {
namespace {

// Returns true if |reason| requires that network validation be entirely
// restarted with the latest IP configuration settings.
bool ShouldResetNetworkValidation(NetworkMonitor::ValidationReason reason) {
  // Only reset PortalDetector if there was an IP provisioning event.
  return reason == NetworkMonitor::ValidationReason::kNetworkConnectionUpdate;
}

// Returns true if |reason| requires that the next network validation attempt
// be scheduled immediately.
bool ShouldScheduleNetworkValidationImmediately(
    NetworkMonitor::ValidationReason reason) {
  switch (reason) {
    case NetworkMonitor::ValidationReason::kDBusRequest:
    case NetworkMonitor::ValidationReason::kEthernetGatewayReachable:
    case NetworkMonitor::ValidationReason::kNetworkConnectionUpdate:
    case NetworkMonitor::ValidationReason::kServiceReorder:
      return true;
    case NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable:
    case NetworkMonitor::ValidationReason::kManagerPropertyUpdate:
    case NetworkMonitor::ValidationReason::kServicePropertyUpdate:
    case NetworkMonitor::ValidationReason::kRetryValidation:
      return false;
  }
}

std::optional<net_base::IPFamily> GetNetworkValidationIPFamily(
    const net_base::NetworkConfig& network_config) {
  if (network_config.ipv4_address) {
    return net_base::IPFamily::kIPv4;
  }
  if (!network_config.ipv6_addresses.empty()) {
    return net_base::IPFamily::kIPv6;
  }
  return std::nullopt;
}

std::vector<net_base::IPAddress> GetNetworkValidationDNSServers(
    const net_base::NetworkConfig& network_config, net_base::IPFamily family) {
  std::vector<net_base::IPAddress> dns_list;
  for (const auto& addr : network_config.dns_servers) {
    if (addr.GetFamily() == family) {
      dns_list.push_back(addr);
    }
  }
  return dns_list;
}

}  // namespace

NetworkMonitor::NetworkMonitor(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    ClientNetwork* client,
    Technology technology,
    int interface_index,
    std::string_view interface,
    PortalDetector::ProbingConfiguration probing_configuration,
    std::unique_ptr<ValidationLog> network_validation_log,
    std::string_view logging_tag,
    std::unique_ptr<PortalDetectorFactory> portal_detector_factory,
    std::unique_ptr<ConnectionDiagnosticsFactory>
        connection_diagnostics_factory)
    : dispatcher_(dispatcher),
      metrics_(metrics),
      client_(client),
      technology_(technology),
      interface_index_(interface_index),
      interface_(std::string(interface)),
      logging_tag_(std::string(logging_tag)),
      probing_configuration_(probing_configuration),
      portal_detector_factory_(std::move(portal_detector_factory)),
      validation_log_(std::move(network_validation_log)),
      connection_diagnostics_factory_(
          std::move(connection_diagnostics_factory)) {}

NetworkMonitor::~NetworkMonitor() {
  StopNetworkValidationLog();
}

bool NetworkMonitor::Start(ValidationReason reason) {
  const net_base::NetworkConfig& config = client_->GetCurrentConfig();
  const std::optional<net_base::IPFamily> ip_family =
      GetNetworkValidationIPFamily(config);
  if (!ip_family) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << "(" << reason
               << "): Cannot start portal detection: No valid IP address";
    return false;
  }
  const std::vector<net_base::IPAddress> dns_list =
      GetNetworkValidationDNSServers(config, *ip_family);
  if (dns_list.empty()) {
    LOG(ERROR) << logging_tag_ << " " << __func__ << "(" << reason
               << "): Cannot start portal detection: No DNS servers";
    return false;
  }

  // Create a new PortalDetector instance and start the first trial if portal
  // detection:
  //   - has not been initialized yet,
  //   - or has stopped,
  //   - or should be reset immediately entirely.
  if (!portal_detector_ || ShouldResetNetworkValidation(reason)) {
    portal_detector_ = portal_detector_factory_->Create(
        dispatcher_, probing_configuration_,
        base::BindRepeating(&NetworkMonitor::OnPortalDetectorResult,
                            weak_ptr_factory_.GetWeakPtr()));
  }

  // Otherwise, if the validation reason requires an immediate restart, reset
  // the delay scheduled between attempts.
  if (ShouldScheduleNetworkValidationImmediately(reason)) {
    portal_detector_->ResetAttemptDelays();
  }

  // If portal detection is not running, reschedule the next a trial.
  if (portal_detector_->IsInProgress()) {
    LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
              << "): Portal detection is already running.";
    return true;
  }

  portal_detector_->Start(interface_, *ip_family, dns_list, logging_tag_);
  LOG(INFO) << logging_tag_ << " " << __func__ << "(" << reason
            << "): Portal detection started.";
  return true;
}

bool NetworkMonitor::Stop() {
  if (!portal_detector_) {
    return false;
  }
  portal_detector_.reset();
  return true;
}

bool NetworkMonitor::IsRunning() const {
  return portal_detector_ != nullptr;
}

void NetworkMonitor::SetCapportAPI(const net_base::HttpUrl& capport_api,
                                   CapportSource source) {
  if (validation_log_) {
    switch (source) {
      case CapportSource::kDHCP:
        validation_log_->SetCapportDHCPSupported();
        break;
      case CapportSource::kRA:
        validation_log_->SetCapportRASupported();
        break;
    }
  }

  // TODO(b/305129516): Initiate CapportClient.
}

void NetworkMonitor::OnPortalDetectorResult(
    const PortalDetector::Result& result) {
  if (validation_log_) {
    validation_log_->AddResult(result);
  }

  const int64_t total_duration =
      std::max(result.http_duration.InMilliseconds(),
               result.https_duration.InMilliseconds());
  switch (result.GetValidationState()) {
    case PortalDetector::ValidationState::kNoConnectivity:
      // If network validation cannot verify Internet access, then start
      // additional connection diagnostics for the current network connection.
      StartConnectionDiagnostics();
      break;
    case PortalDetector::ValidationState::kInternetConnectivity:
      metrics_->SendToUMA(Metrics::kPortalDetectorInternetValidationDuration,
                          technology_, total_duration);
      // Stop recording results in |network_validation_log_| as soon as the
      // first kInternetConnectivity result is observed.
      StopNetworkValidationLog();
      break;
    case PortalDetector::ValidationState::kPortalRedirect:
      metrics_->SendToUMA(Metrics::kPortalDetectorPortalDiscoveryDuration,
                          technology_, total_duration);
      break;
    case PortalDetector::ValidationState::kPortalSuspected:
      break;
  }
  if (result.http_duration.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorHTTPProbeDuration, technology_,
                        result.http_duration.InMilliseconds());
  }
  if (result.https_duration.is_positive()) {
    metrics_->SendToUMA(Metrics::kPortalDetectorHTTPSProbeDuration, technology_,
                        result.https_duration.InMilliseconds());
  }
  if (const auto http_response_code =
          result.GetHTTPResponseCodeMetricResult()) {
    metrics_->SendSparseToUMA(Metrics::kPortalDetectorHTTPResponseCode,
                              technology_, *http_response_code);
  }
  if (result.http_status_code == brillo::http::status_code::Ok &&
      result.http_content_length) {
    metrics_->SendToUMA(Metrics::kPortalDetectorHTTPResponseContentLength,
                        technology_, *result.http_content_length);
  }

  client_->OnNetworkMonitorResult(Result::FromPortalDetectorResult(result));
}

void NetworkMonitor::StopNetworkValidationLog() {
  if (validation_log_) {
    validation_log_->RecordMetrics();
    validation_log_.reset();
  }
}

void NetworkMonitor::StartConnectionDiagnostics() {
  const net_base::NetworkConfig& config = client_->GetCurrentConfig();

  std::optional<net_base::IPAddress> local_address = std::nullopt;
  std::optional<net_base::IPAddress> gateway_address = std::nullopt;
  if (config.ipv4_address) {
    local_address = net_base::IPAddress(config.ipv4_address->address());
    gateway_address =
        config.ipv4_gateway
            ? std::make_optional(net_base::IPAddress(*config.ipv4_gateway))
            : std::nullopt;
  } else if (!config.ipv6_addresses.empty()) {
    local_address = net_base::IPAddress(config.ipv6_addresses[0].address());
    gateway_address =
        config.ipv6_gateway
            ? std::make_optional(net_base::IPAddress(*config.ipv6_gateway))
            : std::nullopt;
  }

  if (!local_address) {
    LOG(ERROR)
        << logging_tag_ << " " << __func__
        << ": Local address unavailable, aborting connection diagnostics";
    return;
  }
  if (!gateway_address) {
    LOG(ERROR) << logging_tag_ << " " << __func__
               << ": Gateway unavailable, aborting connection diagnostics";
    return;
  }

  connection_diagnostics_ = connection_diagnostics_factory_->Create(
      interface_, interface_index_, *local_address, *gateway_address,
      config.dns_servers, dispatcher_, metrics_, base::DoNothing());
  if (!connection_diagnostics_->Start(probing_configuration_.portal_http_url)) {
    connection_diagnostics_.reset();
    LOG(WARNING) << logging_tag_ << " " << __func__
                 << ": Failed to start connection diagnostics";
    return;
  }
  LOG(INFO) << logging_tag_ << " " << __func__
            << ": Connection diagnostics started";
}

void NetworkMonitor::set_portal_detector_for_testing(
    std::unique_ptr<PortalDetector> portal_detector) {
  portal_detector_ = std::move(portal_detector);
}

std::unique_ptr<NetworkMonitor> NetworkMonitorFactory::Create(
    EventDispatcher* dispatcher,
    Metrics* metrics,
    NetworkMonitor::ClientNetwork* client,
    Technology technology,
    int interface_index,
    std::string_view interface,
    PortalDetector::ProbingConfiguration probing_configuration,
    std::unique_ptr<ValidationLog> network_validation_log,
    std::string_view logging_tag) {
  return std::make_unique<NetworkMonitor>(
      dispatcher, metrics, client, technology, interface_index, interface,
      probing_configuration, std::move(network_validation_log), logging_tag);
}

std::ostream& operator<<(std::ostream& stream,
                         NetworkMonitor::ValidationReason reason) {
  switch (reason) {
    case NetworkMonitor::ValidationReason::kNetworkConnectionUpdate:
      return stream << "NetworkConnectionUpdate";
    case NetworkMonitor::ValidationReason::kServiceReorder:
      return stream << "ServiceReorder";
    case NetworkMonitor::ValidationReason::kServicePropertyUpdate:
      return stream << "ServicePropertyUpdate";
    case NetworkMonitor::ValidationReason::kManagerPropertyUpdate:
      return stream << "ManagerPropertyUpdate";
    case NetworkMonitor::ValidationReason::kDBusRequest:
      return stream << "DbusRequest";
    case NetworkMonitor::ValidationReason::kEthernetGatewayUnreachable:
      return stream << "EthernetGatewayUnreachable";
    case NetworkMonitor::ValidationReason::kEthernetGatewayReachable:
      return stream << "EthernetGatewayReachable";
    case NetworkMonitor::ValidationReason::kRetryValidation:
      return stream << "RetryValidation";
  }
}

NetworkMonitor::Result NetworkMonitor::Result::FromPortalDetectorResult(
    const PortalDetector::Result& result) {
  return Result{
      .num_attempts = result.num_attempts,
      .validation_state = result.GetValidationState(),
      .probe_result_metric = result.GetResultMetric(),
      .probe_url = result.probe_url,
  };
}

bool NetworkMonitor::Result::operator==(
    const NetworkMonitor::Result& rhs) const = default;

std::ostream& operator<<(std::ostream& stream,
                         const NetworkMonitor::Result& result) {
  stream << "{ num_attempts=" << result.num_attempts;
  stream << ", validation_state=" << result.validation_state;
  stream << ", result_metric=" << result.probe_result_metric;
  if (result.probe_url) {
    stream << ", probe_url=" << result.probe_url->ToString();
  }
  return stream << " }";
}

}  // namespace shill
