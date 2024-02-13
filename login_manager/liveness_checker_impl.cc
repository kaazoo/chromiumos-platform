// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/liveness_checker_impl.h"

#include <signal.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/compiler_specific.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <brillo/files/safe_fd.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

#include "login_manager/process_manager_service_interface.h"

namespace login_manager {

LivenessCheckerImpl::LivenessCheckerImpl(
    ProcessManagerServiceInterface* manager,
    dbus::ObjectProxy* dbus_proxy,
    bool enable_aborting,
    base::TimeDelta interval,
    int retries,
    LoginMetrics* metrics)
    : manager_(manager),
      dbus_proxy_(dbus_proxy),
      proc_directory_("/proc"),
      enable_aborting_(enable_aborting),
      interval_(interval),
      retry_limit_(retries),
      metrics_(metrics) {
  CHECK_GE(retries, 0);
  base::TimeDelta dbus_timeout = interval / (retries + 1);
  CHECK_GE(dbus_timeout.InMilliseconds(), 1);
}

LivenessCheckerImpl::~LivenessCheckerImpl() {
  Stop();
}

void LivenessCheckerImpl::Start() {
  Stop();  // To be certain.
  last_ping_acked_ = true;
  liveness_check_.Reset(
      base::BindOnce(&LivenessCheckerImpl::CheckAndSendLivenessPing,
                     weak_ptr_factory_.GetWeakPtr(), interval_));
  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, liveness_check_.callback(), interval_);
}

void LivenessCheckerImpl::Stop() {
  weak_ptr_factory_.InvalidateWeakPtrs();
  liveness_check_.Cancel();
}

bool LivenessCheckerImpl::IsRunning() {
  return !liveness_check_.IsCancelled();
}

void LivenessCheckerImpl::DisableAborting() {
  enable_aborting_ = false;
}

void LivenessCheckerImpl::CheckAndSendLivenessPing(base::TimeDelta interval) {
  // If there's an un-acked ping, the browser needs to be taken down.
  if (!last_ping_acked_) {
    LOG(WARNING) << "Browser hang detected!";
    metrics_->SendLivenessPingResult(/*success=*/false);

    // TODO(https://crbug.com/883029): Remove.
    std::string top_output;
    base::GetAppOutput({"top", "-b", "-c", "-n1", "-w512"}, &top_output);

    std::vector<std::string> top_output_lines = base::SplitString(
        top_output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (top_output_lines.size() > 20)
      top_output_lines.resize(20);
    top_output = base::JoinString(top_output_lines, "\n");

    LOG(WARNING) << "Top output (trimmed):";
    LOG(WARNING) << top_output;

    RecordStateForTimeout(/*verbose=*/true);

    if (enable_aborting_) {
      // Note: If this log message is changed, the desktopui_HangDetector
      // autotest must be updated.
      LOG(WARNING) << "Aborting browser process.";

      manager_->AbortBrowserForHang();
      // HandleChildExit() will reap the process and restart if needed.
      Stop();
      return;
    }
  }

  ping_sent_ = base::TimeTicks::Now();
  remaining_retries_ = retry_limit_;
  SendPing(interval_ / (retry_limit_ + 1));
  DVLOG(1) << "Scheduling liveness check in " << interval.InSeconds() << "s.";
  liveness_check_.Reset(
      base::BindOnce(&LivenessCheckerImpl::CheckAndSendLivenessPing,
                     weak_ptr_factory_.GetWeakPtr(), interval));
  brillo::MessageLoop::current()->PostDelayedTask(
      FROM_HERE, liveness_check_.callback(), interval);
}

void LivenessCheckerImpl::HandleAck(dbus::Response* response) {
  base::TimeDelta response_time = base::TimeTicks::Now() - ping_sent_;
  if (response != nullptr) {
    last_ping_acked_ = true;
    metrics_->SendLivenessPingResponseTime(response_time);
    metrics_->SendLivenessPingResult(/*success=*/true);
    return;
  }
  if (remaining_retries_) {
    RecordStateForTimeout(/*verbose=*/false);
    remaining_retries_--;
    base::TimeDelta dbus_timeout =
        std::min(interval_ - response_time, interval_ / (retry_limit_ + 1));
    if (dbus_timeout >= base::Milliseconds(1)) {
      SendPing(dbus_timeout);
    }
  }
}

void LivenessCheckerImpl::SendPing(base::TimeDelta dbus_timeout) {
  last_ping_acked_ = false;
  dbus::MethodCall ping(chromeos::kLivenessServiceInterface,
                        chromeos::kLivenessServiceCheckLivenessMethod);
  dbus_proxy_->CallMethod(&ping, dbus_timeout.InMilliseconds(),
                          base::BindOnce(&LivenessCheckerImpl::HandleAck,
                                         weak_ptr_factory_.GetWeakPtr()));
}

void LivenessCheckerImpl::SetProcForTests(base::FilePath&& proc_directory) {
  proc_directory_ = std::move(proc_directory);
}

LoginMetrics::BrowserState LivenessCheckerImpl::GetBrowserState() {
  std::optional<std::string> state = ReadBrowserProcFile("status");
  if (!state) {
    return LoginMetrics::BrowserState::kErrorGettingState;
  }

  const std::string kStateField = "\nState:\t";
  std::string::const_iterator state_begin = std::search(
      state->begin(), state->end(), kStateField.begin(), kStateField.end());

  if (state_begin == state->end()) {
    LOG(WARNING) << "Could not find '\\nState:\\t' in /proc/pid/status";
    return LoginMetrics::BrowserState::kErrorGettingState;
  }

  std::string::const_iterator state_end = state_begin + kStateField.length();

  if (state_end == state->end()) {
    LOG(WARNING) << "State:\\t at very end of file";
    return LoginMetrics::BrowserState::kErrorGettingState;
  }

  switch (*state_end) {
    case 'R':
      return LoginMetrics::BrowserState::kRunning;
    case 'S':
      return LoginMetrics::BrowserState::kSleeping;
    case 'D':
      return LoginMetrics::BrowserState::kUninterruptibleWait;
    case 'Z':
      return LoginMetrics::BrowserState::kZombie;
    case 'T':
      return LoginMetrics::BrowserState::kTracedOrStopped;
    default:
      LOG(WARNING) << "Unknown browser state " << *state_end;
      return LoginMetrics::BrowserState::kUnknown;
  }
}

std::optional<std::string> LivenessCheckerImpl::ReadBrowserProcFile(
    std::string_view filename) {
  std::optional<pid_t> browser_pid = manager_->GetBrowserPid();
  if (!browser_pid.has_value()) {
    return std::nullopt;
  }

  base::FilePath file_path(proc_directory_);
  file_path = file_path.Append(base::NumberToString(browser_pid.value()))
                  .Append(filename);

  brillo::SafeFD::SafeFDResult fd_result = brillo::SafeFD::Root();
  if (brillo::SafeFD::IsError(fd_result.second)) {
    PLOG(WARNING) << "Could not get root directory "
                  << static_cast<int>(fd_result.second) << ": ";
    return std::nullopt;
  }

  fd_result = fd_result.first.OpenExistingFile(file_path, O_RDONLY | O_CLOEXEC);
  if (brillo::SafeFD::IsError(fd_result.second)) {
    PLOG(WARNING) << "Could not open " << file_path.value() << " error code "
                  << static_cast<int>(fd_result.second) << ": ";
    return std::nullopt;
  }

  std::pair<std::vector<char>, brillo::SafeFD::Error> result =
      fd_result.first.ReadContents();
  if (brillo::SafeFD::IsError(result.second)) {
    LOG(WARNING) << "Failed to read proc file: " << filename
                 << " error: " << static_cast<int>(result.second);
    return std::nullopt;
  }
  return std::string(result.first.begin(), result.first.end());
}

void LivenessCheckerImpl::RecordKernelStack(LoginMetrics::BrowserState state) {
  std::optional<std::string> stack = ReadBrowserProcFile("stack");
  if (!stack) {
    return;
  }
  LOG(WARNING) << "browser stack for state " << static_cast<int>(state) << ": "
               << *stack;
}

void LivenessCheckerImpl::RecordWchanState(LoginMetrics::BrowserState state) {
  std::optional<std::string> wchan = ReadBrowserProcFile("wchan");
  if (!wchan) {
    return;
  }

  // TODO(iby): Add a UMA here.
  // Ideally, we'd like to increment a UMA histogram based on which syscall
  // Chrome is waiting for. Unfortunately, there are about 400 system calls in
  // Linux, which is well above our normal histogram limit, and they are not
  // consistent between kernels and architectures, so making an exhaustive list
  // and having it consistent for all machines is a lot of code. Instead, for
  // now, we just dump the contents to the log file. Once we have some logs,
  // I'll add a histogram with a somewhat adhoc list of entries that are showing
  // up most frequently.
  LOG(WARNING) << "browser wchan for state " << static_cast<int>(state) << ": "
               << *wchan;
}

void LivenessCheckerImpl::RequestKernelTraces() {
  base::FilePath file_path(proc_directory_);
  file_path = file_path.Append("sysrq-trigger");

  brillo::SafeFD::SafeFDResult result = brillo::SafeFD::Root();
  if (brillo::SafeFD::IsError(result.second)) {
    PLOG(WARNING) << "Could not get root directory "
                  << static_cast<int>(result.second) << ": ";
    return;
  }

  result = result.first.OpenExistingFile(file_path, O_WRONLY | O_CLOEXEC);
  if (brillo::SafeFD::IsError(result.second)) {
    PLOG(WARNING) << "Could not open sysrq-trigger file "
                  << static_cast<int>(result.second) << ": ";
    return;
  }

  // Don't use SafeFD::Replace here; we don't want to try and truncate the
  // sysrq-trigger file (which SafeFD::Replace does).
  // Order is important: w and m are synchronous, l is not, so if we do l before
  // one of the others, all the lines get mixed together.
  const char kShowMemoryRequest[] = "m";
  if (!base::WriteFileDescriptor(result.first.get(), kShowMemoryRequest)) {
    PLOG(WARNING) << "Failed to write 'm' to sysrq-trigger file: ";
  }

  const char kShowBlockedTasksRequest[] = "w";
  if (!base::WriteFileDescriptor(result.first.get(),
                                 kShowBlockedTasksRequest)) {
    PLOG(WARNING) << "Failed to write 'w' to sysrq-trigger file: ";
  }

  const char kShowStackBacktraceRequest[] = "l";
  if (!base::WriteFileDescriptor(result.first.get(),
                                 kShowStackBacktraceRequest)) {
    PLOG(WARNING) << "Failed to write 'l' to sysrq-trigger file: ";
  }
}

void LivenessCheckerImpl::RecordStateForTimeout(bool verbose) {
  LoginMetrics::BrowserState state = GetBrowserState();
  // If the browser is currently running there's no point in trying to dump its
  // state.
  if (state != LoginMetrics::BrowserState::kSleeping &&
      state != LoginMetrics::BrowserState::kUninterruptibleWait &&
      state != LoginMetrics::BrowserState::kTracedOrStopped) {
    return;
  }
  if (verbose) {
    RecordWchanState(state);
    RequestKernelTraces();
  } else {
    RecordKernelStack(state);
  }
}

}  // namespace login_manager
