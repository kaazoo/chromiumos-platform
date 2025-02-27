// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_PAIRING_H_
#define DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_PAIRING_H_

#include <optional>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/types/expected.h>
#include <brillo/variant_dictionary.h>

#include "diagnostics/cros_healthd/routines/bluetooth/floss/bluetooth_base.h"
#include "diagnostics/cros_healthd/routines/noninteractive_routine_control.h"
#include "diagnostics/mojom/public/cros_healthd_routines.mojom-forward.h"

namespace diagnostics {
class Context;
namespace floss {
enum class BtPropertyType : uint32_t;
enum class BondState : uint32_t;

// This routine is supported when ChromeOS is using Floss instead of Bluez.
//
// The Bluetooth scanning routine checks that the Bluetooth adapter can scan
// nearby Bluetooth peripherals and collect nearby peripherals' information.
class BluetoothPairingRoutine final : public NoninteractiveRoutineControl,
                                      public BluetoothRoutineBase {
 public:
  explicit BluetoothPairingRoutine(
      Context* context,
      const ash::cros_healthd::mojom::BluetoothPairingRoutineArgumentPtr& arg);
  BluetoothPairingRoutine(const BluetoothPairingRoutine&) = delete;
  BluetoothPairingRoutine& operator=(const BluetoothPairingRoutine&) = delete;
  ~BluetoothPairingRoutine() override;

  // BaseRoutineControl overrides:
  void OnStart() override;

 private:
  void RunNextStep();

  // Helper function to ensure the default adapter is not null.
  org::chromium::bluetooth::BluetoothProxyInterface* GetDefaultAdapterOrStop();

  // Handle the response of routine initialization.
  void HandleInitializeResult(bool success);

  // Handle the response of pre-check.
  void HandlePreCheckResponse(std::optional<std::string> error);

  // Handle the response of changing powered state.
  void HandleEnsurePoweredOnResponse(std::optional<bool> dbus_powered);

  // Check if the target peripheral is bonded.
  void CheckTargetPeripheralBonded(
      brillo::Error* error,
      const std::vector<brillo::VariantDictionary>& devices);

  // Handle the response of updating discovery mode.
  void HandleUpdateDiscoveryResponse(brillo::Error* error, bool is_success);

  // Observe the device events of scan the target device.
  void OnDeviceAdded(const brillo::VariantDictionary& device);
  void OnDevicePropertyChanged(const brillo::VariantDictionary& device,
                               BtPropertyType property);

  // Handle the response of updating device alias.
  void HandleUpdateAliasResponse(brillo::Error* error);

  // Get the device properties of the target peripheral.
  void GetDeviceProperties();
  // Store the device's UUIDs.
  void StoreDeviceUuids(brillo::Error* error,
                        const std::vector<std::vector<uint8_t>>& uuids);
  // Store the device's Bluetooth class.
  void StoreDeviceClass(brillo::Error* error, uint32_t bluetooth_class);
  // Store the device's address type and do the address validation.
  void StoreDeviceAddressType(brillo::Error* error, uint32_t address_type);

  // Handle the response of bonding the target peripheral.
  void HandleBondDeviceResponse(brillo::Error* error, uint32_t bt_status);

  // Observe the device connected event to check if the basebend connection is
  // established successfully.
  void OnDeviceConnectedChanged(const brillo::VariantDictionary& device,
                                bool connected);
  void HandleConnectionStateResponse(brillo::Error* error, uint32_t state);

  // Observe the device SSP (Secure Simple Pairing) events to set pairing
  // confirmation for target peripheral.
  void OnDeviceSspRequest(const brillo::VariantDictionary& device);
  // Handle the response of setting pairing confirmation for target peripheral.
  void HandlePairingConfirmationResponse(brillo::Error* error, bool is_success);

  // Observe the device bond state events to check if the device is bonded
  // successfully.
  void OnDeviceBondChanged(uint32_t bt_status,
                           const std::string& address,
                           BondState bond_state);

  // Handle the response of removing the target peripheral's bond.
  void HandleRemoveBondResponse(brillo::Error* error, bool is_success);

  // Update the routine percentage.
  void UpdatePercentage();

  // Routine timeout function.
  void OnTimeoutOccurred();

  // Set the routine result and stop other callbacks.
  void SetResultAndStop(const base::expected<bool, std::string>& result);

  enum class TestStep : int32_t {
    kInitialize = 0,
    kPreCheckDiscovery = 1,
    kEnsurePoweredOn = 2,
    kCheckBondedDevices = 3,
    kStartDiscovery = 4,
    kScanTargetDevice = 5,
    kTagTargetDevice = 6,
    kCollectDeviceInfo = 7,
    kBondTargetDevice = 8,
    kCollectDeviceInfoAfterPaired = 9,
    kResetDeviceTag = 10,
    kRemoveTargetDevice = 11,
    kComplete = 12,  // Should be the last one. New step should be added before
                     // it.
  };
  TestStep step_ = TestStep::kInitialize;

  // The device dictionary for the target peripheral.
  brillo::VariantDictionary target_device_;

  // Routine arguments:
  // Peripheral ID of routine's target peripheral.
  const std::string peripheral_id_;

  // Detail of routine output.
  ash::cros_healthd::mojom::BluetoothPairingRoutineDetailPtr routine_output_;

  // A callback that should be run regardless of the execution status. This
  // callback will remove the target peripheral.
  base::ScopedClosureRunner remove_target_peripheral_;

  // Must be the last class member.
  base::WeakPtrFactory<BluetoothPairingRoutine> weak_ptr_factory_{this};
};

}  // namespace floss
}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_ROUTINES_BLUETOOTH_FLOSS_BLUETOOTH_PAIRING_H_
