// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_IMPL_H_

#include <map>
#include <memory>

#include <base/files/file_descriptor_watcher_posix.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote_set.h>

#include "diagnostics/cros_healthd/events/udev_events.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

class UdevEventsImpl final : public UdevEvents {
 public:
  explicit UdevEventsImpl(Context* context);
  UdevEventsImpl(const UdevEventsImpl&) = delete;
  UdevEventsImpl& operator=(const UdevEventsImpl&) = delete;
  ~UdevEventsImpl() override = default;

  bool Initialize() override;
  void AddThunderboltObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;
  void AddUsbObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;
  void AddSdCardObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;
  void AddHdmiObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::EventObserver> observer)
      override;

  void OnUdevEvent();

  // Old interfaces that are going to be deprecated.
  void AddThunderboltObserver(
      mojo::PendingRemote<
          ash::cros_healthd::mojom::CrosHealthdThunderboltObserver> observer)
      override;
  void AddUsbObserver(
      mojo::PendingRemote<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
          observer) override;

 private:
  void OnThunderboltAddEvent();
  void OnThunderboltRemoveEvent();
  void OnThunderboltAuthorizedEvent();
  void OnThunderboltUnAuthorizedEvent();

  void OnUsbAdd(const std::unique_ptr<brillo::UdevDevice>& device);
  void OnUsbRemove(const std::unique_ptr<brillo::UdevDevice>& device);

  void OnSdCardAdd();
  void OnSdCardRemove();

  void OnHdmiChange();

  // Unowned pointer. Should outlive this instance.
  Context* const context_ = nullptr;

  std::unique_ptr<base::FileDescriptorWatcher::Controller>
      udev_monitor_watcher_;
  // Stores the last known connection status for HDMI connectors.
  std::map<uint32_t, bool> hdmi_connector_status_;
  // Each observer in |thunderbolt_observers_| will be notified of any
  // thunderbolt event in the
  // ash::cros_healthd::mojom::CrosHealthdThunderboltObserver interface.
  // The RemoteSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::RemoteSet<ash::cros_healthd::mojom::EventObserver>
      thunderbolt_observers_;
  mojo::RemoteSet<ash::cros_healthd::mojom::CrosHealthdThunderboltObserver>
      deprecated_thunderbolt_observers_;
  // Each observer in |usb_observers_| will be notified of any usb event in the
  // ash::cros_healthd::mojom::CrosHealthdUsbObserver interface. The
  // RemoteSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::RemoteSet<ash::cros_healthd::mojom::EventObserver> usb_observers_;
  mojo::RemoteSet<ash::cros_healthd::mojom::CrosHealthdUsbObserver>
      deprecated_usb_observers_;

  // Each observer in |sd_card_observers_| will be notified of any SD Card
  // event. The RemoteSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::RemoteSet<ash::cros_healthd::mojom::EventObserver> sd_card_observers_;

  // Each observer in |hdmi_observers_| will be notified of any SD Card
  // event. The RemoteSet manages the lifetime of the endpoints, which are
  // automatically destroyed and removed when the pipe they are bound to is
  // destroyed.
  mojo::RemoteSet<ash::cros_healthd::mojom::EventObserver> hdmi_observers_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EVENTS_UDEV_EVENTS_IMPL_H_
