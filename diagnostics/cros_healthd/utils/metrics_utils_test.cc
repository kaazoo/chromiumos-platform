// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/utils/metrics_utils.h"

#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <base/functional/bind.h>
#include <base/test/mock_callback.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "diagnostics/cros_healthd/utils/metrics_utils_constants.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

using ::testing::_;
using ::testing::Return;

namespace diagnostics {
namespace {

namespace mojom = ::ash::cros_healthd::mojom;

using OnTerminalStatusCallback =
    base::OnceCallback<void(mojom::DiagnosticRoutineStatusEnum)>;

class MetricsUtilsTest : public ::testing::Test {
 protected:
  template <typename T>
  void ExpectSendEnumToUMA(const std::string& name, T sample) {
    EXPECT_CALL(metrics_library_,
                SendEnumToUMA(name, static_cast<int>(sample), _))
        .WillOnce(Return(true));
  }

  void ExpectNoSendEnumToUMA() {
    EXPECT_CALL(metrics_library_, SendEnumToUMA(_, _, _)).Times(0);
  }

  void SendTelemetryResult(const std::set<mojom::ProbeCategoryEnum>& categories,
                           const mojom::TelemetryInfoPtr& info) {
    SendTelemetryResultToUMA(&metrics_library_, categories, info);
  }

  void SendDiagnosticResult(mojom::DiagnosticRoutineEnum routine,
                            mojom::DiagnosticRoutineStatusEnum status) {
    SendDiagnosticResultToUMA(&metrics_library_, routine, status);
  }

  void SendEventCategory(mojom::EventCategoryEnum category) {
    SendEventSubscriptionUsageToUMA(&metrics_library_, category);
  }

  testing::StrictMock<MetricsLibraryMock> metrics_library_;
};

TEST_F(MetricsUtilsTest, InvokeOnTerminalStatusForTerminalStatus) {
  base::MockCallback<OnTerminalStatusCallback> callback;
  EXPECT_CALL(callback, Run(mojom::DiagnosticRoutineStatusEnum::kPassed))
      .Times(1);
  auto wrapped_callback = InvokeOnTerminalStatus(callback.Get());
  wrapped_callback.Run(mojom::DiagnosticRoutineStatusEnum::kRunning);
  wrapped_callback.Run(mojom::DiagnosticRoutineStatusEnum::kPassed);
}

TEST_F(MetricsUtilsTest, InvokeOnTerminalStatusForNonTerminalStatus) {
  base::MockCallback<OnTerminalStatusCallback> callback;
  EXPECT_CALL(callback, Run).Times(0);
  auto wrapped_callback = InvokeOnTerminalStatus(callback.Get());
  wrapped_callback.Run(mojom::DiagnosticRoutineStatusEnum::kWaiting);
}

// Passing in two terminal status should invoke the callback only once.
TEST_F(MetricsUtilsTest, InvokeOnTerminalStatusOnlyOnce) {
  base::MockCallback<OnTerminalStatusCallback> callback;
  EXPECT_CALL(callback, Run(mojom::DiagnosticRoutineStatusEnum::kPassed))
      .Times(1);
  auto wrapped_callback = InvokeOnTerminalStatus(callback.Get());
  wrapped_callback.Run(mojom::DiagnosticRoutineStatusEnum::kPassed);
  wrapped_callback.Run(mojom::DiagnosticRoutineStatusEnum::kError);
}

TEST_F(MetricsUtilsTest, SendNoTelemetryResultForUnknownCategory) {
  ExpectNoSendEnumToUMA();
  SendTelemetryResult({mojom::ProbeCategoryEnum::kUnknown},
                      mojom::TelemetryInfo::New());
}

TEST_F(MetricsUtilsTest, SendBatteryTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBattery,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->battery_result = mojom::BatteryResult::NewBatteryInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBattery}, info);
}

TEST_F(MetricsUtilsTest, SendCpuTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultCpu,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->cpu_result = mojom::CpuResult::NewCpuInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kCpu}, info);
}

TEST_F(MetricsUtilsTest, SendBlockDeviceTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBlockDevice,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->block_device_result =
      mojom::NonRemovableBlockDeviceResult::NewBlockDeviceInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kNonRemovableBlockDevices},
                      info);
}

TEST_F(MetricsUtilsTest, SendTimezoneTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultTimezone,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->timezone_result = mojom::TimezoneResult::NewTimezoneInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kTimezone}, info);
}

TEST_F(MetricsUtilsTest, SendMemoryTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultMemory,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->memory_result = mojom::MemoryResult::NewMemoryInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kMemory}, info);
}

TEST_F(MetricsUtilsTest, SendBacklightTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBacklight,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->backlight_result = mojom::BacklightResult::NewBacklightInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBacklight}, info);
}

TEST_F(MetricsUtilsTest, SendFanTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultFan,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->fan_result = mojom::FanResult::NewFanInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kFan}, info);
}

TEST_F(MetricsUtilsTest, SendStatefulPartitionTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultStatefulPartition,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->stateful_partition_result =
      mojom::StatefulPartitionResult::NewPartitionInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kStatefulPartition}, info);
}

TEST_F(MetricsUtilsTest, SendBluetoothTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBluetooth,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->bluetooth_result = mojom::BluetoothResult::NewBluetoothAdapterInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBluetooth}, info);
}

TEST_F(MetricsUtilsTest, SendSystemTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultSystem,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->system_result = mojom::SystemResult::NewSystemInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kSystem}, info);
}

TEST_F(MetricsUtilsTest, SendNetworkTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultNetwork,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->network_result = mojom::NetworkResult::NewNetworkHealth({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kNetwork}, info);
}

TEST_F(MetricsUtilsTest, SendAudioTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultAudio,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->audio_result = mojom::AudioResult::NewAudioInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kAudio}, info);
}

TEST_F(MetricsUtilsTest, SendBootPerformanceTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBootPerformance,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->boot_performance_result =
      mojom::BootPerformanceResult::NewBootPerformanceInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBootPerformance}, info);
}

TEST_F(MetricsUtilsTest, SendBusTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBus,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->bus_result = mojom::BusResult::NewBusDevices({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBus}, info);
}

TEST_F(MetricsUtilsTest, SendTpmTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultTpm,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->tpm_result = mojom::TpmResult::NewTpmInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kTpm}, info);
}

TEST_F(MetricsUtilsTest, SendNetworkInterfaceTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultNetworkInterface,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->network_interface_result =
      mojom::NetworkInterfaceResult::NewNetworkInterfaceInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kNetworkInterface}, info);
}

TEST_F(MetricsUtilsTest, SendGraphicsTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultGraphics,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->graphics_result = mojom::GraphicsResult::NewGraphicsInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kGraphics}, info);
}

TEST_F(MetricsUtilsTest, SendDisplayTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultDisplay,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->display_result = mojom::DisplayResult::NewDisplayInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kDisplay}, info);
}

TEST_F(MetricsUtilsTest, SendInputTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultInput,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->input_result = mojom::InputResult::NewInputInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kInput}, info);
}

TEST_F(MetricsUtilsTest, SendAudioHardwareTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultAudioHardware,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->audio_hardware_result =
      mojom::AudioHardwareResult::NewAudioHardwareInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kAudioHardware}, info);
}

TEST_F(MetricsUtilsTest, SendSensorTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultSensor,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->sensor_result = mojom::SensorResult::NewSensorInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kSensor}, info);
}

TEST_F(MetricsUtilsTest, SendThermalTelemetryResult) {
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultThermal,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->thermal_result = mojom::ThermalResult::NewThermalInfo({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kThermal}, info);
}

TEST_F(MetricsUtilsTest, SendMultipleTelemetryResult) {
  // The choice of categories is arbitrary.
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBattery,
                      CrosHealthdTelemetryResult::kSuccess);
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultCpu,
                      CrosHealthdTelemetryResult::kSuccess);
  auto info = mojom::TelemetryInfo::New();
  info->battery_result = mojom::BatteryResult::NewBatteryInfo({});
  info->cpu_result = mojom::CpuResult::NewCpuInfo({});
  SendTelemetryResult(
      {
          mojom::ProbeCategoryEnum::kBattery,
          mojom::ProbeCategoryEnum::kCpu,
      },
      info);
}

TEST_F(MetricsUtilsTest, SendTelemetryErrorResult) {
  // The choice of category is arbitrary.
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBattery,
                      CrosHealthdTelemetryResult::kError);
  auto info = mojom::TelemetryInfo::New();
  info->battery_result = mojom::BatteryResult::NewError({});
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBattery}, info);
}

TEST_F(MetricsUtilsTest, SendTelemetryResultWithANullField) {
  // The choice of category is arbitrary.
  ExpectSendEnumToUMA(metrics_name::kTelemetryResultBattery,
                      CrosHealthdTelemetryResult::kError);
  auto info = mojom::TelemetryInfo::New();
  SendTelemetryResult({mojom::ProbeCategoryEnum::kBattery}, info);
}

struct RoutineMetricNameTestCase {
  mojom::DiagnosticRoutineEnum routine;
  std::optional<std::string> metrics;
};

const RoutineMetricNameTestCase routine_metric_name_test_cases[] = {
    {
        .routine = mojom::DiagnosticRoutineEnum::kUnknown,
        .metrics = std::nullopt,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBatteryCapacity,
        .metrics = metrics_name::kDiagnosticResultBatteryCapacity,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBatteryHealth,
        .metrics = metrics_name::kDiagnosticResultBatteryHealth,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kUrandom,
        .metrics = metrics_name::kDiagnosticResultUrandom,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kSmartctlCheck,
        .metrics = metrics_name::kDiagnosticResultSmartctlCheck,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kAcPower,
        .metrics = metrics_name::kDiagnosticResultAcPower,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kCpuCache,
        .metrics = metrics_name::kDiagnosticResultCpuCache,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kCpuStress,
        .metrics = metrics_name::kDiagnosticResultCpuStress,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kFloatingPointAccuracy,
        .metrics = metrics_name::kDiagnosticResultFloatingPointAccuracy,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::DEPRECATED_kNvmeWearLevel,
        .metrics = metrics_name::kDiagnosticResultNvmeWearLevel,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kNvmeSelfTest,
        .metrics = metrics_name::kDiagnosticResultNvmeSelfTest,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kDiskRead,
        .metrics = metrics_name::kDiagnosticResultDiskRead,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kPrimeSearch,
        .metrics = metrics_name::kDiagnosticResultPrimeSearch,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBatteryDischarge,
        .metrics = metrics_name::kDiagnosticResultBatteryDischarge,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBatteryCharge,
        .metrics = metrics_name::kDiagnosticResultBatteryCharge,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kMemory,
        .metrics = metrics_name::kDiagnosticResultMemory,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kLanConnectivity,
        .metrics = metrics_name::kDiagnosticResultLanConnectivity,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kSignalStrength,
        .metrics = metrics_name::kDiagnosticResultSignalStrength,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kGatewayCanBePinged,
        .metrics = metrics_name::kDiagnosticResultGatewayCanBePinged,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kHasSecureWiFiConnection,
        .metrics = metrics_name::kDiagnosticResultHasSecureWiFiConnection,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kDnsResolverPresent,
        .metrics = metrics_name::kDiagnosticResultDnsResolverPresent,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kDnsLatency,
        .metrics = metrics_name::kDiagnosticResultDnsLatency,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kDnsResolution,
        .metrics = metrics_name::kDiagnosticResultDnsResolution,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kCaptivePortal,
        .metrics = metrics_name::kDiagnosticResultCaptivePortal,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kHttpFirewall,
        .metrics = metrics_name::kDiagnosticResultHttpFirewall,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kHttpsFirewall,
        .metrics = metrics_name::kDiagnosticResultHttpsFirewall,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kHttpsLatency,
        .metrics = metrics_name::kDiagnosticResultHttpsLatency,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kVideoConferencing,
        .metrics = metrics_name::kDiagnosticResultVideoConferencing,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kArcHttp,
        .metrics = metrics_name::kDiagnosticResultArcHttp,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kArcPing,
        .metrics = metrics_name::kDiagnosticResultArcPing,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kArcDnsResolution,
        .metrics = metrics_name::kDiagnosticResultArcDnsResolution,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kSensitiveSensor,
        .metrics = metrics_name::kDiagnosticResultSensitiveSensor,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kFingerprint,
        .metrics = metrics_name::kDiagnosticResultFingerprint,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kFingerprintAlive,
        .metrics = metrics_name::kDiagnosticResultFingerprintAlive,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kPrivacyScreen,
        .metrics = metrics_name::kDiagnosticResultPrivacyScreen,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kLedLitUp,
        .metrics = metrics_name::kDiagnosticResultLedLitUp,
    },
    {
        .routine =
            mojom::DiagnosticRoutineEnum::kSmartctlCheckWithPercentageUsed,
        .metrics =
            metrics_name::kDiagnosticResultSmartctlCheckWithPercentageUsed,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kEmmcLifetime,
        .metrics = metrics_name::kDiagnosticResultEmmcLifetime,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::DEPRECATED_kAudioSetVolume,
        .metrics = metrics_name::kDiagnosticResultAudioSetVolume,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::DEPRECATED_kAudioSetGain,
        .metrics = metrics_name::kDiagnosticResultAudioSetGain,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBluetoothPower,
        .metrics = metrics_name::kDiagnosticResultBluetoothPower,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBluetoothDiscovery,
        .metrics = metrics_name::kDiagnosticResultBluetoothDiscovery,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBluetoothScanning,
        .metrics = metrics_name::kDiagnosticResultBluetoothScanning,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kBluetoothPairing,
        .metrics = metrics_name::kDiagnosticResultBluetoothPairing,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kPowerButton,
        .metrics = metrics_name::kDiagnosticResultPowerButton,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kAudioDriver,
        .metrics = metrics_name::kDiagnosticResultAudioDriver,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kUfsLifetime,
        .metrics = metrics_name::kDiagnosticResultUfsLifetime,
    },
    {
        .routine = mojom::DiagnosticRoutineEnum::kFan,
        .metrics = metrics_name::kDiagnosticResultFan,
    },
};

TEST_F(MetricsUtilsTest, AllRoutineMetricNamesTested) {
  EXPECT_EQ(std::size(routine_metric_name_test_cases),
            static_cast<int32_t>(mojom::DiagnosticRoutineEnum::kMaxValue) -
                static_cast<int32_t>(mojom::DiagnosticRoutineEnum::kMinValue) +
                1);
}

class RoutineMetricNameTest
    : public MetricsUtilsTest,
      public ::testing::WithParamInterface<RoutineMetricNameTestCase> {};

TEST_P(RoutineMetricNameTest, SendDiagnosticResult) {
  // The choice of diagnostic result is arbitrary.
  const RoutineMetricNameTestCase& test_case = GetParam();
  if (test_case.metrics.has_value()) {
    ExpectSendEnumToUMA(test_case.metrics.value(),
                        CrosHealthdDiagnosticResult::kPassed);
  } else {
    ExpectNoSendEnumToUMA();
  }
  SendDiagnosticResult(test_case.routine,
                       mojom::DiagnosticRoutineStatusEnum::kPassed);
}

INSTANTIATE_TEST_SUITE_P(
    AllRoutine,
    RoutineMetricNameTest,
    ::testing::ValuesIn<RoutineMetricNameTestCase>(
        routine_metric_name_test_cases),
    [](const testing::TestParamInfo<RoutineMetricNameTest::ParamType>& info) {
      std::stringstream ss;
      ss << info.param.routine;
      return ss.str();
    });

struct DiagnosticResultTestCase {
  mojom::DiagnosticRoutineStatusEnum diag_result;
  std::optional<CrosHealthdDiagnosticResult> uma_value;
};

constexpr DiagnosticResultTestCase diag_result_test_cases[] = {
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kPassed,
        .uma_value = CrosHealthdDiagnosticResult::kPassed,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kFailed,
        .uma_value = CrosHealthdDiagnosticResult::kFailed,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kError,
        .uma_value = CrosHealthdDiagnosticResult::kError,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kCancelled,
        .uma_value = CrosHealthdDiagnosticResult::kCancelled,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kFailedToStart,
        .uma_value = CrosHealthdDiagnosticResult::kFailedToStart,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kRemoved,
        .uma_value = CrosHealthdDiagnosticResult::kRemoved,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kUnsupported,
        .uma_value = CrosHealthdDiagnosticResult::kUnsupported,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kNotRun,
        .uma_value = CrosHealthdDiagnosticResult::kNotRun,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kUnknown,
        .uma_value = std::nullopt,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kReady,
        .uma_value = std::nullopt,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kRunning,
        .uma_value = std::nullopt,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kWaiting,
        .uma_value = std::nullopt,
    },
    {
        .diag_result = mojom::DiagnosticRoutineStatusEnum::kCancelling,
        .uma_value = std::nullopt,
    }};

TEST_F(MetricsUtilsTest, AllDiagnosticResultTested) {
  EXPECT_EQ(
      std::size(diag_result_test_cases),
      static_cast<int32_t>(mojom::DiagnosticRoutineStatusEnum::kMaxValue) -
          static_cast<int32_t>(mojom::DiagnosticRoutineStatusEnum::kMinValue) +
          1);
}

class DiagnosticResultTest
    : public MetricsUtilsTest,
      public ::testing::WithParamInterface<DiagnosticResultTestCase> {};

TEST_P(DiagnosticResultTest, SendDiagnosticResult) {
  // The choice of routine is arbitrary.
  const DiagnosticResultTestCase& test_case = GetParam();
  if (test_case.uma_value.has_value()) {
    ExpectSendEnumToUMA(metrics_name::kDiagnosticResultBatteryCapacity,
                        test_case.uma_value.value());
  } else {
    ExpectNoSendEnumToUMA();
  }
  SendDiagnosticResult(mojom::DiagnosticRoutineEnum::kBatteryCapacity,
                       test_case.diag_result);
}

INSTANTIATE_TEST_SUITE_P(
    AllDiagnosticResult,
    DiagnosticResultTest,
    ::testing::ValuesIn<DiagnosticResultTestCase>(diag_result_test_cases),
    [](const testing::TestParamInfo<DiagnosticResultTest::ParamType>& info) {
      std::stringstream ss;
      ss << info.param.diag_result;
      return ss.str();
    });

TEST_F(MetricsUtilsTest, SendNoUmaForUnrecognizedEventCategory) {
  ExpectNoSendEnumToUMA();
  SendEventCategory(mojom::EventCategoryEnum::kUnmappedEnumField);
}

struct EventCategoryTestCase {
  CrosHealthdEventCategory uma_value;
  mojom::EventCategoryEnum category;
};

class EventCategoryTest
    : public MetricsUtilsTest,
      public ::testing::WithParamInterface<EventCategoryTestCase> {};

// Verify that the UMA enum value matches the event category.
TEST_P(EventCategoryTest, SendEventCategory) {
  const EventCategoryTestCase& test_case = GetParam();
  ExpectSendEnumToUMA(metrics_name::kEventSubscription, test_case.uma_value);
  SendEventCategory(test_case.category);
}

INSTANTIATE_TEST_SUITE_P(
    AllEventCategory,
    EventCategoryTest,
    ::testing::ValuesIn<EventCategoryTestCase>(
        {{.uma_value = CrosHealthdEventCategory::kUsb,
          .category = mojom::EventCategoryEnum::kUsb},
         {.uma_value = CrosHealthdEventCategory::kThunderbolt,
          .category = mojom::EventCategoryEnum::kThunderbolt},
         {.uma_value = CrosHealthdEventCategory::kLid,
          .category = mojom::EventCategoryEnum::kLid},
         {.uma_value = CrosHealthdEventCategory::kBluetooth,
          .category = mojom::EventCategoryEnum::kBluetooth},
         {.uma_value = CrosHealthdEventCategory::kPower,
          .category = mojom::EventCategoryEnum::kPower},
         {.uma_value = CrosHealthdEventCategory::kAudio,
          .category = mojom::EventCategoryEnum::kAudio},
         {.uma_value = CrosHealthdEventCategory::kAudioJack,
          .category = mojom::EventCategoryEnum::kAudioJack},
         {.uma_value = CrosHealthdEventCategory::kSdCard,
          .category = mojom::EventCategoryEnum::kSdCard},
         {.uma_value = CrosHealthdEventCategory::kNetwork,
          .category = mojom::EventCategoryEnum::kNetwork},
         {.uma_value = CrosHealthdEventCategory::kKeyboardDiagnostic,
          .category = mojom::EventCategoryEnum::kKeyboardDiagnostic},
         {.uma_value = CrosHealthdEventCategory::kTouchpad,
          .category = mojom::EventCategoryEnum::kTouchpad},
         {.uma_value = CrosHealthdEventCategory::kExternalDisplay,
          .category = mojom::EventCategoryEnum::kExternalDisplay},
         {.uma_value = CrosHealthdEventCategory::kTouchscreen,
          .category = mojom::EventCategoryEnum::kTouchscreen},
         {.uma_value = CrosHealthdEventCategory::kStylusGarage,
          .category = mojom::EventCategoryEnum::kStylusGarage},
         {.uma_value = CrosHealthdEventCategory::kStylus,
          .category = mojom::EventCategoryEnum::kStylus},
         {.uma_value = CrosHealthdEventCategory::kCrash,
          .category = mojom::EventCategoryEnum::kCrash}}),
    [](const testing::TestParamInfo<EventCategoryTest::ParamType>& info) {
      std::stringstream ss;
      ss << info.param.category;
      return ss.str();
    });

}  // namespace
}  // namespace diagnostics
