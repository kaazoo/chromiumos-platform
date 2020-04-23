// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_DBUS_BIOMETRICS_MANAGER_PROXY_BASE_H_
#define BIOD_DBUS_BIOMETRICS_MANAGER_PROXY_BASE_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_manager.h>

#include "biod/proto_bindings/constants.pb.h"

namespace biod {

const char* ScanResultToString(ScanResult result);

class BiometricsManagerProxyBase {
 public:
  using FinishCallback = base::Callback<void(bool success)>;
  using SignalCallback = dbus::ObjectProxy::SignalCallback;
  using OnConnectedCallback = dbus::ObjectProxy::OnConnectedCallback;

  // Factory method. Returns nullptr if cannot get a dbus proxy for biod.
  static std::unique_ptr<BiometricsManagerProxyBase> Create(
      const scoped_refptr<dbus::Bus>& bus, const dbus::ObjectPath& path);

  void ConnectToAuthScanDoneSignal(SignalCallback signal_callback,
                                   OnConnectedCallback on_connected_callback);

  const dbus::ObjectPath path() const;

  void SetFinishHandler(const FinishCallback& on_finish);

  // Starts biometrics auth session synchronously.
  bool StartAuthSession();

  // Starts biometrics auth session asynchronously.
  // |callback| is called when starting the auth session succeeds/fails.
  void StartAuthSessionAsync(base::Callback<void(bool success)> callback);

  // Ends biometrics auth session and resets state.
  void EndAuthSession();

 protected:
  BiometricsManagerProxyBase();

  bool Initialize(const scoped_refptr<dbus::Bus>& bus,
                  const dbus::ObjectPath& path);

  void OnFinish(bool success);

  void OnSignalConnected(const std::string& interface,
                         const std::string& signal,
                         bool success);

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_;

 private:
  friend class BiometricsManagerProxyBaseTest;

  void OnSessionFailed(dbus::Signal* signal);

  // Handler for StartAuthSessionAsync. |callback| will be called on behalf of
  // the caller of StartAuthSessionAsync.
  void OnStartAuthSessionResp(base::Callback<void(bool success)> callback,
                              dbus::Response* response);

  // Parse a dbus response and return the ObjectProxy implied by the response.
  // Returns nullptr on error.
  dbus::ObjectProxy* HandleAuthSessionResponse(dbus::Response* response);

  FinishCallback on_finish_;

  base::WeakPtrFactory<BiometricsManagerProxyBase> weak_factory_;

  dbus::ObjectProxy* biod_auth_session_;

  DISALLOW_COPY_AND_ASSIGN(BiometricsManagerProxyBase);
};

}  // namespace biod

#endif  // BIOD_DBUS_BIOMETRICS_MANAGER_PROXY_BASE_H_
