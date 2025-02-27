// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_

#include <map>
#include <string>

#include "diagnostics/cros_healthd/system/usb_device_info.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_exception.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace diagnostics {
class Context;
class CrosConfig;
class PathLiteral;
struct FingerprintParameter;

class GroundTruth final {
 public:
  explicit GroundTruth(Context* context);
  GroundTruth(const GroundTruth&) = delete;
  GroundTruth& operator=(const GroundTruth&) = delete;
  ~GroundTruth();

  void IsEventSupported(ash::cros_healthd::mojom::EventCategoryEnum category,
                        ash::cros_healthd::mojom::CrosHealthdEventService::
                            IsEventSupportedCallback callback);

  // These methods check if a routine is supported and prepare its parameters
  // from system configurations.
  // The naming should be `PrepareRoutine{RotuineName}`. They return
  // `mojom::SupportStatusPtr` and routine parameters, if any, via output
  // arguments.
  //
  // Please update docs/routine_supportability.md if the supportability
  // definition of a routine has changed. Add "NO_IFTTT=<reason>" in the commit
  // message if it's not applicable.
  //
  // LINT.IfChange
  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineBatteryCapacity(
      std::optional<uint32_t>& low_mah,
      std::optional<uint32_t>& high_mah) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineBatteryHealth(
      std::optional<uint32_t>& maximum_cycle_count,
      std::optional<uint8_t>& percent_battery_wear_allowed) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutinePrimeSearch(
      std::optional<uint64_t>& max_num) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineNvmeWearLevel(
      std::optional<uint32_t>& threshold) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineFingerprint(
      FingerprintParameter& param) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineUfsLifetime() const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineFan(
      uint8_t& fan_count) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineVolumeButton() const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineLedLitUp() const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineKeyboardBacklight()
      const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineCameraAvailability()
      const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineEmmcLifetime() const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineNetworkBandwidth(
      std::string& oem_name) const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineCameraFrameAnalysis()
      const;

  ash::cros_healthd::mojom::SupportStatusPtr PrepareRoutineBatteryDischarge()
      const;

  using PrepareRoutineBluetoothFlossCallback =
      base::OnceCallback<void(ash::cros_healthd::mojom::SupportStatusPtr)>;
  void PrepareRoutineBluetoothFloss(
      PrepareRoutineBluetoothFlossCallback callback) const;
  // LINT.ThenChange(//diagnostics/docs/routine_supportability.md)

  // Check if the device has CrOS EC.
  bool HasCrosEC() const;

  // Sets the map within the USB Device Info object, dictating the USB device
  // type with given {VID:PID}.
  void SetUsbDeviceInfoEntryForTesting(
      std::map<std::string, DeviceType> entries);

  // Returns whether the given USB device is a SD Card Reader device.
  bool IsSdCardDevice(const std::string& vendor_id,
                      const std::string& product_id);

 private:
  ash::cros_healthd::mojom::SupportStatusPtr GetEventSupportStatus(
      ash::cros_healthd::mojom::EventCategoryEnum category);

  CrosConfig* cros_config() const;

  // Maps the {vid:pid} of USB devices to the actual underlying media type.
  USBDeviceInfo usb_device_info_;

  // Unowned. Should outlive this instance.
  Context* const context_ = nullptr;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_GROUND_TRUTH_H_
