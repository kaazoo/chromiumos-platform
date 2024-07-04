// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/network/https_latency.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>

#include "diagnostics/cros_healthd/routines/simple_routine.h"
#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/mojom/external/network_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;
namespace network_diagnostics_ipc = ::chromeos::network_diagnostics::mojom;

std::string GetProblemMessage(
    network_diagnostics_ipc::HttpsLatencyProblem problem) {
  switch (problem) {
    case network_diagnostics_ipc::HttpsLatencyProblem::kFailedDnsResolutions:
      return kHttpsLatencyRoutineFailedDnsResolutionsProblemMessage;
    case network_diagnostics_ipc::HttpsLatencyProblem::kFailedHttpsRequests:
      return kHttpsLatencyRoutineFailedHttpsRequestsProblemMessage;
    case network_diagnostics_ipc::HttpsLatencyProblem::kHighLatency:
      return kHttpsLatencyRoutineHighLatencyProblemMessage;
    case network_diagnostics_ipc::HttpsLatencyProblem::kVeryHighLatency:
      return kHttpsLatencyRoutineVeryHighLatencyProblemMessage;
  }
}

// Parses the results of the HTTPS latency routine.
SimpleRoutine::RoutineResult ParseHttpsLatencyResult(
    network_diagnostics_ipc::RoutineResultPtr result) {
  switch (result->verdict) {
    case network_diagnostics_ipc::RoutineVerdict::kNoProblem:
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kPassed,
          .status_message = kHttpsLatencyRoutineNoProblemMessage,
      };
    case network_diagnostics_ipc::RoutineVerdict::kNotRun:
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kNotRun,
          .status_message = kHttpsLatencyRoutineNotRunMessage,
      };
    case network_diagnostics_ipc::RoutineVerdict::kProblem:
      auto problems = result->problems->get_https_latency_problems();
      DCHECK(!problems.empty());
      return {
          .status = mojom::DiagnosticRoutineStatusEnum::kFailed,
          .status_message = GetProblemMessage(problems[0]),
      };
  }
}

void RunHttpsLatencyRoutine(MojoService* const mojo_service,
                            SimpleRoutine::RoutineResultCallback callback) {
  auto* network_diagnostics_routines =
      mojo_service->GetNetworkDiagnosticsRoutines();
  if (!network_diagnostics_routines) {
    std::move(callback).Run({
        .status = mojom::DiagnosticRoutineStatusEnum::kNotRun,
        .status_message = kHttpsLatencyRoutineNotRunMessage,
    });
    return;
  }
  network_diagnostics_routines->RunHttpsLatency(
      network_diagnostics_ipc::RoutineCallSource::kCrosHealthd,
      base::BindOnce(&ParseHttpsLatencyResult).Then(std::move(callback)));
}

}  // namespace

const char kHttpsLatencyRoutineNoProblemMessage[] =
    "HTTPS latency routine passed with no problems.";
const char kHttpsLatencyRoutineFailedDnsResolutionsProblemMessage[] =
    "One or more DNS resolutions resulted in a failure.";
const char kHttpsLatencyRoutineFailedHttpsRequestsProblemMessage[] =
    "One or more HTTPS requests resulted in a failure.";
const char kHttpsLatencyRoutineHighLatencyProblemMessage[] =
    "HTTPS request latency is high.";
const char kHttpsLatencyRoutineVeryHighLatencyProblemMessage[] =
    "HTTPS request latency is very high.";
const char kHttpsLatencyRoutineNotRunMessage[] =
    "HTTPS latency routine did not run.";

std::unique_ptr<DiagnosticRoutine> CreateHttpsLatencyRoutine(
    MojoService* const mojo_service) {
  return std::make_unique<SimpleRoutine>(
      base::BindOnce(&RunHttpsLatencyRoutine, mojo_service));
}

}  // namespace diagnostics
