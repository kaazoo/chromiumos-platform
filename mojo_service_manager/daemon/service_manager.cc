// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo_service_manager/daemon/service_manager.h"

#include <set>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "mojo_service_manager/daemon/mojo_error_util.h"

namespace chromeos::mojo_service_manager {

ServiceManager::ServiceState::ServiceState(const std::string& service_name,
                                           ServicePolicy policy)
    : policy(std::move(policy)), request_queue(service_name) {}

ServiceManager::ServiceState::~ServiceState() = default;

ServiceManager::ServiceManager(Configuration configuration,
                               ServicePolicyMap policy_map)
    : configuration_(std::move(configuration)) {
  for (auto& item : policy_map) {
    auto& [service_name, policy] = item;
    // The elements of ServiceState is not moveable. Use |try_emplace| to
    // construct it in place.
    service_map_.try_emplace(service_name, service_name, std::move(policy));
  }

  receiver_set_.set_disconnect_handler(base::BindRepeating(
      &ServiceManager::HandleDisconnect, base::Unretained(this)));
}

ServiceManager::~ServiceManager() = default;

void ServiceManager::AddReceiver(
    mojom::ProcessIdentityPtr process_identity,
    mojo::PendingReceiver<mojom::ServiceManager> receiver) {
  receiver_set_.Add(this, std::move(receiver), std::move(process_identity));
}

void ServiceManager::Register(
    const std::string& service_name,
    mojo::PendingRemote<mojom::ServiceProvider> service_provider) {
  auto it = service_map_.find(service_name);
  if (it == service_map_.end()) {
    if (!configuration_.is_permissive) {
      LOG(ERROR) << "Cannot find service " << service_name;
      service_provider.ResetWithReason(
          static_cast<uint32_t>(mojom::ErrorCode::kServiceNotFound),
          "Cannot find service " + service_name);
      return;
    }
    // In permissive mode, users are allowed to register a service which is not
    // in the policy. In this case, a new ServiceState needs to be created.
    auto [it_new, success] =
        service_map_.try_emplace(service_name, service_name, ServicePolicy{});
    CHECK(success);
    it = it_new;
  }

  ServiceState& service_state = it->second;
  const mojom::ProcessIdentityPtr& identity = receiver_set_.current_context();
  if (!configuration_.is_permissive &&
      !service_state.policy.IsOwnerUid(identity->uid) &&
      !service_state.policy.IsOwner(identity->security_context)) {
    auto error = base::StringPrintf(
        "The user %d(%s) and the security context %s are not allowed to own "
        "the service %s.",
        identity->uid, identity->username.value_or("unknown user").c_str(),
        identity->security_context.c_str(), service_name.c_str());
    LOG(ERROR) << error;
    service_provider.ResetWithReason(
        static_cast<uint32_t>(mojom::ErrorCode::kPermissionDenied), error);
    return;
  }
  if (service_state.service_provider.is_bound()) {
    LOG(ERROR) << "The service " << service_name
               << " has already been registered.";
    service_provider.ResetWithReason(
        static_cast<uint32_t>(mojom::ErrorCode::kServiceAlreadyRegistered),
        "The service " + service_name + " has already been registered.");
    return;
  }
  service_state.service_provider.Bind(std::move(service_provider));
  service_state.service_provider.set_disconnect_handler(
      base::BindOnce(&ServiceManager::ServiceProviderDisconnectHandler,
                     weak_factory_.GetWeakPtr(), service_name));

  service_state.owner = identity.Clone();
  SendServiceEvent(
      service_state.policy.requesters_uid(), service_state.policy.requesters(),
      mojom::ServiceEvent::New(mojom::ServiceEvent::Type::kRegistered,
                               service_name, identity.Clone()));

  for (ServiceRequestQueue::ServiceRequest& request :
       service_state.request_queue.TakeAllRequests()) {
    // If a receiver become invalid before being posted, don't send it because
    // the mojo will complain about sending invalid handles and reset the
    // connection of service provider.
    if (!request.receiver.is_valid()) {
      continue;
    }
    service_state.service_provider->Request(std::move(request.identity),
                                            std::move(request.receiver));
  }
}

void ServiceManager::Request(const std::string& service_name,
                             std::optional<base::TimeDelta> timeout,
                             mojo::ScopedMessagePipeHandle receiver) {
  auto it = service_map_.find(service_name);
  if (it == service_map_.end()) {
    if (!configuration_.is_permissive) {
      LOG(ERROR) << "Cannot find service " << service_name;
      ResetMojoReceiverPipeWithReason(std::move(receiver),
                                      mojom::ErrorCode::kServiceNotFound,
                                      "Cannot find service " + service_name);
      return;
    }
    // In permissive mode, users are allowed to request a service which is not
    // in the policy. In this case, a new ServiceState needs to be created.
    auto [it_new, success] =
        service_map_.try_emplace(service_name, service_name, ServicePolicy{});
    CHECK(success);
    it = it_new;
  }

  ServiceState& service_state = it->second;
  const mojom::ProcessIdentityPtr& identity = receiver_set_.current_context();
  if (!configuration_.is_permissive &&
      !service_state.policy.IsRequesterUid(identity->uid) &&
      !service_state.policy.IsRequester(identity->security_context)) {
    auto error = base::StringPrintf(
        "The user %d(%s) and the security context %s are not allowed to "
        "request the service %s.",
        identity->uid, identity->username.value_or("unknown user").c_str(),
        identity->security_context.c_str(), service_name.c_str());
    LOG(ERROR) << error;
    ResetMojoReceiverPipeWithReason(std::move(receiver),
                                    mojom::ErrorCode::kPermissionDenied, error);
    return;
  }
  if (service_state.service_provider.is_bound()) {
    service_state.service_provider->Request(identity.Clone(),
                                            std::move(receiver));
    return;
  }
  service_state.request_queue.Push(identity.Clone(), std::move(timeout),
                                   std::move(receiver));
}

void ServiceManager::Query(const std::string& service_name,
                           QueryCallback callback) {
  auto it = service_map_.find(service_name);
  if (it == service_map_.end()) {
    std::move(callback).Run(mojom::ErrorOrServiceState::NewError(
        mojom::Error::New(mojom::ErrorCode::kServiceNotFound,
                          "Cannot find service " + service_name)));
    return;
  }

  const ServiceState& service_state = it->second;
  const mojom::ProcessIdentityPtr& identity = receiver_set_.current_context();
  if (!configuration_.is_permissive &&
      !service_state.policy.IsRequesterUid(identity->uid) &&
      !service_state.policy.IsRequester(identity->security_context)) {
    std::move(callback).Run(
        mojom::ErrorOrServiceState::NewError(mojom::Error::New(
            mojom::ErrorCode::kPermissionDenied,
            "The security context " + identity->security_context +
                " is not allowed to query the service " + service_name)));
    return;
  }
  mojom::ServiceStatePtr state =
      service_state.owner.is_null()
          ? mojom::ServiceState::NewUnregisteredState(
                mojom::UnregisteredServiceState::New())
          : mojom::ServiceState::NewRegisteredState(
                mojom::RegisteredServiceState::New(
                    /*owner=*/service_state.owner.Clone()));
  std::move(callback).Run(
      mojom::ErrorOrServiceState::NewState(std::move(state)));
}

void ServiceManager::AddServiceObserver(
    mojo::PendingRemote<mojom::ServiceObserver> observer) {
  const mojom::ProcessIdentityPtr& identity = receiver_set_.current_context();
  auto id = service_observer_map_[identity->uid].Add(std::move(observer));
  service_observer_map_legacy_[identity->security_context].insert(
      std::make_pair(identity->uid, id));

  service_observer_map_[identity->uid].set_disconnect_handler(
      base::BindRepeating(&ServiceManager::HandleObserverDisconnect,
                          weak_factory_.GetWeakPtr(), identity->uid));
}

void ServiceManager::ServiceProviderDisconnectHandler(
    const std::string& service_name) {
  auto it = service_map_.find(service_name);
  CHECK(it != service_map_.end());
  ServiceState& service_state = it->second;
  service_state.service_provider.reset();
  mojom::ProcessIdentityPtr dispatcher;
  dispatcher.Swap(&service_state.owner);
  SendServiceEvent(
      service_state.policy.requesters_uid(), service_state.policy.requesters(),
      mojom::ServiceEvent::New(mojom::ServiceEvent::Type::kUnRegistered,
                               service_name, std::move(dispatcher)));
}

void ServiceManager::SendServiceEvent(
    const std::set<uint32_t>& requesters_uid,
    const std::set<std::string>& requesters_selinux,
    mojom::ServiceEventPtr event) {
  if (configuration_.is_permissive) {
    // In permissive mode, all the observer can receive the event.
    for (const auto& item : service_observer_map_) {
      for (const mojo::Remote<mojom::ServiceObserver>& remote : item.second) {
        remote->OnServiceEvent(event.Clone());
      }
    }
    return;
  }
  for (const uint32_t& uid : requesters_uid) {
    auto it = service_observer_map_.find(uid);
    if (it == service_observer_map_.end()) {
      continue;
    }
    for (const mojo::Remote<mojom::ServiceObserver>& remote : it->second) {
      remote->OnServiceEvent(event.Clone());
    }
  }
  // TODO(b/333323875): Remove this selinux workaround.
  for (const std::string& security_context : requesters_selinux) {
    auto it = service_observer_map_legacy_.find(security_context);
    if (it == service_observer_map_legacy_.end()) {
      continue;
    }
    for (const auto& [uid, remote_id] : it->second) {
      service_observer_map_[uid].Get(remote_id)->OnServiceEvent(event.Clone());
    }
  }
}

void ServiceManager::HandleDisconnect() {
  LOG(INFO) << "Disconnected from "
            << receiver_set_.current_context()->security_context << ", "
            << receiver_set_.current_context()->uid << "("
            << receiver_set_.current_context()->username.value_or(
                   "unknown user")
            << ")";
}

void ServiceManager::HandleObserverDisconnect(uint32_t uid,
                                              mojo::RemoteSetElementId id) {
  // Iterate through all legacy map to find the disconnected remote id.
  // TODO(b/333323875): Remove this selinux workaround.
  for (auto& [unused_security_context, remote_id_set] :
       service_observer_map_legacy_) {
    if (remote_id_set.erase(std::make_pair(uid, id)) == 1) {
      return;
    }
  }
}

}  // namespace chromeos::mojo_service_manager
