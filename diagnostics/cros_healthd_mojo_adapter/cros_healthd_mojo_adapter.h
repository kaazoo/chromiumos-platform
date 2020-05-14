// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/optional.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate.h"
#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate_impl.h"
#include "mojo/cros_healthd.mojom.h"
#include "mojo/cros_healthd_diagnostics.mojom.h"
#include "mojo/cros_healthd_events.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// Provides a mojo connection to cros_healthd. See mojo/cros_healthd.mojom for
// details on cros_healthd's mojo interface. This should only be used by
// processes whose only mojo connection is to cros_healthd.
class CrosHealthdMojoAdapter final {
 public:
  // Override |delegate| for testing only.
  explicit CrosHealthdMojoAdapter(
      CrosHealthdMojoAdapterDelegate* delegate = nullptr);
  ~CrosHealthdMojoAdapter();

  // Gets telemetry information from cros_healthd.
  chromeos::cros_healthd::mojom::TelemetryInfoPtr GetTelemetryInfo(
      const std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>&
          categories_to_probe);

  // Runs the urandom routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunUrandomRoutine(
      uint32_t length_seconds);

  // Runs the battery capacity routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryCapacityRoutine(uint32_t low_mah, uint32_t high_mah);

  // Runs the battery health routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunBatteryHealthRoutine(
      uint32_t maximum_cycle_count, uint32_t percent_battery_wear_allowed);

  // Runs the smartctl-check routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunSmartctlCheckRoutine();

  // Runs the AC power routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunAcPowerRoutine(
      chromeos::cros_healthd::mojom::AcPowerStatusEnum expected_status,
      const base::Optional<std::string>& expected_power_type);

  // Runs the CPU cache routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunCpuCacheRoutine(
      base::TimeDelta exec_duration);

  // Runs the CPU stress routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunCpuStressRoutine(
      base::TimeDelta exec_duration);

  // Runs the NvmeWearLevel routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunNvmeWearLevelRoutine(
      uint32_t wear_level_threshold);

  // Runs the NvmeSelfTest routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunNvmeSelfTestRoutine(
      chromeos::cros_healthd::mojom::NvmeSelfTestTypeEnum nvme_self_test_type);

  // Runs the disk read routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunDiskReadRoutine(
      chromeos::cros_healthd::mojom::DiskReadRoutineTypeEnum type,
      base::TimeDelta exec_duration,
      uint32_t file_size_mb);

  // Runs the prime search routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr RunPrimeSearchRoutine(
      base::TimeDelta exec_duration, uint64_t max_num);

  // Runs the battery discharge routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunBatteryDischargeRoutine(base::TimeDelta exec_duration,
                             uint32_t maximum_discharge_percent_allowed);

  // Returns which routines are available on the platform.
  std::vector<chromeos::cros_healthd::mojom::DiagnosticRoutineEnum>
  GetAvailableRoutines();

  // Gets an update for the specified routine.
  chromeos::cros_healthd::mojom::RoutineUpdatePtr GetRoutineUpdate(
      int32_t id,
      chromeos::cros_healthd::mojom::DiagnosticRoutineCommandEnum command,
      bool include_output);

  // Runs the floating-point-accuracy routine.
  chromeos::cros_healthd::mojom::RunRoutineResponsePtr
  RunFloatingPointAccuracyRoutine(base::TimeDelta exec_duration);

  // Subscribes the client to Bluetooth events.
  void AddBluetoothObserver(
      chromeos::cros_healthd::mojom::CrosHealthdBluetoothObserverPtr observer);

  // Subscribes the client to lid events.
  void AddLidObserver(
      chromeos::cros_healthd::mojom::CrosHealthdLidObserverPtr observer);

  // Subscribes the client to power events.
  void AddPowerObserver(
      chromeos::cros_healthd::mojom::CrosHealthdPowerObserverPtr observer);

 private:
  // Establishes a mojo connection with cros_healthd.
  void Connect();

  // Default delegate implementation.
  std::unique_ptr<CrosHealthdMojoAdapterDelegateImpl> delegate_impl_;
  // Unowned. Must outlive this instance.
  CrosHealthdMojoAdapterDelegate* delegate_;

  // Binds to an implementation of CrosHealthdServiceFactory. The implementation
  // is provided by cros_healthd. Allows calling cros_healthd's mojo factory
  // methods.
  chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtr
      cros_healthd_service_factory_;
  // Binds to an implementation of CrosHealthdProbeService. The implementation
  // is provided by cros_healthd. Allows calling cros_healthd's probe-related
  // mojo methods.
  chromeos::cros_healthd::mojom::CrosHealthdProbeServicePtr
      cros_healthd_probe_service_;
  // Binds to an implementation of CrosHealthdDiagnosticsService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // diagnostics-related mojo methods.
  chromeos::cros_healthd::mojom::CrosHealthdDiagnosticsServicePtr
      cros_healthd_diagnostics_service_;
  // Binds to an implementation of CrosHealthdEventService. The
  // implementation is provided by cros_healthd. Allows calling cros_healthd's
  // event-related mojo methods.
  chromeos::cros_healthd::mojom::CrosHealthdEventServicePtr
      cros_healthd_event_service_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdMojoAdapter);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_H_
