// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/wilco_dtc_supportd_core_delegate_impl.h"

#include <utility>

#include <base/logging.h>
#include <dbus/wilco_dtc_supportd/dbus-constants.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/edk/embedder/platform_handle.h>
#include <mojo/edk/embedder/scoped_platform_handle.h>

#include "debugd/dbus-proxies.h"
#include "diagnostics/wilco_dtc_supportd/system/bluetooth_client_impl.h"
#include "diagnostics/wilco_dtc_supportd/system/debugd_adapter_impl.h"
#include "diagnostics/wilco_dtc_supportd/system/powerd_adapter_impl.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/bluetooth_event_service_impl.h"
#include "diagnostics/wilco_dtc_supportd/telemetry/powerd_event_service_impl.h"
#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

using MojomWilcoDtcSupportdServiceFactory =
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory;

WilcoDtcSupportdCoreDelegateImpl::WilcoDtcSupportdCoreDelegateImpl(
    brillo::Daemon* daemon)
    : daemon_(daemon) {
  DCHECK(daemon_);
}

WilcoDtcSupportdCoreDelegateImpl::~WilcoDtcSupportdCoreDelegateImpl() = default;

std::unique_ptr<mojo::Binding<MojomWilcoDtcSupportdServiceFactory>>
WilcoDtcSupportdCoreDelegateImpl::BindWilcoDtcSupportdMojoServiceFactory(
    MojomWilcoDtcSupportdServiceFactory* mojo_service_factory,
    base::ScopedFD mojo_pipe_fd) {
  DCHECK(mojo_pipe_fd.is_valid());

  mojo::edk::SetParentPipeHandle(mojo::edk::ScopedPlatformHandle(
      mojo::edk::PlatformHandle(mojo_pipe_fd.release())));

  mojo::ScopedMessagePipeHandle mojo_pipe_handle =
      mojo::edk::CreateChildMessagePipe(
          kWilcoDtcSupportdMojoConnectionChannelToken);
  if (!mojo_pipe_handle.is_valid()) {
    LOG(ERROR) << "Failed to create Mojo child message pipe";
    return nullptr;
  }

  return std::make_unique<mojo::Binding<MojomWilcoDtcSupportdServiceFactory>>(
      mojo_service_factory, std::move(mojo_pipe_handle));
}

void WilcoDtcSupportdCoreDelegateImpl::BeginDaemonShutdown() {
  daemon_->Quit();
}

std::unique_ptr<BluetoothClient>
WilcoDtcSupportdCoreDelegateImpl::CreateBluetoothClient(
    const scoped_refptr<dbus::Bus>& bus) {
  DCHECK(bus);
  return std::make_unique<BluetoothClientImpl>(bus);
}

std::unique_ptr<DebugdAdapter>
WilcoDtcSupportdCoreDelegateImpl::CreateDebugdAdapter(
    const scoped_refptr<dbus::Bus>& bus) {
  DCHECK(bus);
  return std::make_unique<DebugdAdapterImpl>(
      std::make_unique<org::chromium::debugdProxy>(bus));
}

std::unique_ptr<PowerdAdapter>
WilcoDtcSupportdCoreDelegateImpl::CreatePowerdAdapter(
    const scoped_refptr<dbus::Bus>& bus) {
  DCHECK(bus);
  return std::make_unique<PowerdAdapterImpl>(bus);
}

std::unique_ptr<BluetoothEventService>
WilcoDtcSupportdCoreDelegateImpl::CreateBluetoothEventService(
    BluetoothClient* bluetooth_client) {
  DCHECK(bluetooth_client);
  return std::make_unique<BluetoothEventServiceImpl>(bluetooth_client);
}

std::unique_ptr<PowerdEventService>
WilcoDtcSupportdCoreDelegateImpl::CreatePowerdEventService(
    PowerdAdapter* powerd_adapter) {
  DCHECK(powerd_adapter);
  return std::make_unique<PowerdEventServiceImpl>(powerd_adapter);
}

}  // namespace diagnostics
