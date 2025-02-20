// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/postinstall_runner_action.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <unistd.h>

#include <cmath>
#include <iterator>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

#include "update_engine/common/action_processor.h"
#include "update_engine/common/boot_control_interface.h"
#include "update_engine/common/platform_constants.h"
#include "update_engine/common/subprocess.h"
#include "update_engine/common/utils.h"

namespace {

// The file descriptor number from the postinstall program's perspective where
// it can report status updates. This can be any number greater than 2 (stderr),
// but must be kept in sync with the "bin/postinst_progress" defined in the
// sample_images.sh file.
const int kPostinstallStatusFd = 3;

}  // namespace

namespace chromeos_update_engine {

using brillo::MessageLoop;
using std::string;
using std::vector;

void PostinstallRunnerAction::PerformAction() {
  CHECK(HasInputObject());
  install_plan_ = GetInputObject();

  // We always powerwash when rolling back, however policy can determine
  // if this is a full/normal powerwash, or a special rollback powerwash
  // that retains a small amount of system state such as enrollment and
  // network configuration. In both cases all user accounts are deleted.
  if (install_plan_.powerwash_required || install_plan_.is_rollback) {
    if (hardware_->SchedulePowerwash(
            install_plan_.rollback_data_save_requested)) {
      powerwash_scheduled_ = true;
    } else {
      return CompletePostinstall(ErrorCode::kPostinstallPowerwashError);
    }
  }

  // Initialize all the partition weights.
  partition_weight_.resize(install_plan_.partitions.size());
  total_weight_ = 0;
  for (size_t i = 0; i < install_plan_.partitions.size(); ++i) {
    auto& partition = install_plan_.partitions[i];
    if (!install_plan_.run_post_install && partition.postinstall_optional) {
      partition.run_postinstall = false;
      LOG(INFO) << "Skipping optional post-install for partition "
                << partition.name << " according to install plan.";
    }

    // TODO(deymo): This code sets the weight to all the postinstall commands,
    // but we could remember how long they took in the past and use those
    // values.
    partition_weight_[i] = partition.run_postinstall;
    total_weight_ += partition_weight_[i];
  }
  accumulated_weight_ = 0;
  ReportProgress(0);

  PerformPartitionPostinstall();
}

void PostinstallRunnerAction::PerformPartitionPostinstall() {
  switch (install_plan_.defer_update_action) {
    case DeferUpdateAction::kOff:
      if (install_plan_.download_url.empty()) {
        LOG(INFO) << "Skipping post-install during rollback";
        return CompletePostinstall(ErrorCode::kSuccess);
      }
      break;
    case DeferUpdateAction::kHold:
    case DeferUpdateAction::kApplyAndReboot:
    case DeferUpdateAction::kApplyAndShutdown:
      break;
  }

  // Skip all the partitions that don't have a post-install step.
  while (current_partition_ < install_plan_.partitions.size() &&
         !install_plan_.partitions[current_partition_].run_postinstall) {
    VLOG(1) << "Skipping post-install on partition "
            << install_plan_.partitions[current_partition_].name;
    current_partition_++;
  }
  if (current_partition_ == install_plan_.partitions.size()) {
    return CompletePostinstall(ErrorCode::kSuccess);
  }

  const InstallPlan::Partition& partition =
      install_plan_.partitions[current_partition_];

  const string mountable_device = partition.target_path;
  if (mountable_device.empty()) {
    LOG(ERROR) << "Cannot make mountable device from " << partition.target_path;
    return CompletePostinstall(ErrorCode::kPostinstallRunnerError);
  }

  // Perform post-install for the current_partition_ partition. At this point we
  // need to call CompletePartitionPostinstall to complete the operation and
  // cleanup.
  base::FilePath temp_dir;
  TEST_AND_RETURN(base::CreateNewTempDirectory("au_postint_mount", &temp_dir));
  fs_mount_dir_ = temp_dir.value();

  // Double check that the fs_mount_dir is not busy with a previous mounted
  // filesystem from a previous crashed postinstall step.
  if (utils::IsMountpoint(fs_mount_dir_)) {
    LOG(INFO) << "Found previously mounted filesystem at " << fs_mount_dir_;
    utils::UnmountFilesystem(fs_mount_dir_);
  }

  base::FilePath postinstall_path(partition.postinstall_path);
  if (postinstall_path.IsAbsolute()) {
    LOG(ERROR) << "Invalid absolute path passed to postinstall, use a relative"
                  "path instead: "
               << partition.postinstall_path;
    return CompletePostinstall(ErrorCode::kPostinstallRunnerError);
  }

  string abs_path =
      base::FilePath(fs_mount_dir_).Append(postinstall_path).value();
  if (!base::StartsWith(abs_path, fs_mount_dir_,
                        base::CompareCase::SENSITIVE)) {
    LOG(ERROR) << "Invalid relative postinstall path: "
               << partition.postinstall_path;
    return CompletePostinstall(ErrorCode::kPostinstallRunnerError);
  }

  if (!utils::MountFilesystem(mountable_device, fs_mount_dir_, MS_RDONLY,
                              partition.filesystem_type,
                              constants::kPostinstallMountOptions)) {
    return CompletePartitionPostinstall(
        1, "Error mounting the device " + mountable_device);
  }

  LOG(INFO) << "Performing postinst (" << partition.postinstall_path << " at "
            << abs_path << ") installed on device " << partition.target_path
            << " and mountable device " << mountable_device;

  // Logs the file format of the postinstall script we are about to run. This
  // will help debug when the postinstall script doesn't match the architecture
  // of our build.
  LOG(INFO) << "Format file for new " << partition.postinstall_path
            << " is: " << utils::GetFileFormat(abs_path);

  // Runs the postinstall script asynchronously to free up the main loop while
  // it's running.
  vector<string> command = {abs_path};
  // Chrome OS postinstall expects the target rootfs as the first parameter.
  command.push_back(partition.target_path);

  // Defer update action to apply.
  switch (install_plan_.defer_update_action) {
    case DeferUpdateAction::kOff:
      break;
    case DeferUpdateAction::kHold:
      LOG(INFO) << "Defer update action: hold";
      command.push_back("--defer_update_action=hold");
      break;
    case DeferUpdateAction::kApplyAndReboot:
    case DeferUpdateAction::kApplyAndShutdown:
      LOG(INFO) << "Defer update action: apply";
      command.push_back("--defer_update_action=apply");
      break;
  }

  // Force fw update if requested.
  if (force_fw_update_) {
    LOG(INFO) << "Forcing firmware update.";
    command.push_back("--force_update_firmware");
  } else {
    LOG(INFO) << "Not forcing firmware update.";
  }

  current_command_ = Subprocess::Get().ExecFlags(
      command, Subprocess::kRedirectStderrToStdout, {kPostinstallStatusFd},
      base::BindOnce(&PostinstallRunnerAction::CompletePartitionPostinstall,
                     base::Unretained(this)));
  // Subprocess::Exec should never return a negative process id.
  CHECK_GE(current_command_, 0);

  if (!current_command_) {
    CompletePartitionPostinstall(1, "Postinstall didn't launch");
    return;
  }

  // Monitor the status file descriptor.
  progress_fd_ =
      Subprocess::Get().GetPipeFd(current_command_, kPostinstallStatusFd);
  int fd_flags = fcntl(progress_fd_, F_GETFL, 0) | O_NONBLOCK;
  if (HANDLE_EINTR(fcntl(progress_fd_, F_SETFL, fd_flags)) < 0) {
    PLOG(ERROR) << "Unable to set non-blocking I/O mode on fd " << progress_fd_;
  }

  progress_controller_ = base::FileDescriptorWatcher::WatchReadable(
      progress_fd_,
      base::BindRepeating(&PostinstallRunnerAction::OnProgressFdReady,
                          base::Unretained(this)));
}

void PostinstallRunnerAction::OnProgressFdReady() {
  char buf[1024];
  size_t bytes_read;
  do {
    bytes_read = 0;
    bool eof;
    bool ok =
        utils::ReadAll(progress_fd_, buf, std::size(buf), &bytes_read, &eof);
    progress_buffer_.append(buf, bytes_read);
    // Process every line.
    vector<string> lines = base::SplitString(
        progress_buffer_, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
    if (!lines.empty()) {
      progress_buffer_ = lines.back();
      lines.pop_back();
      for (const auto& line : lines) {
        ProcessProgressLine(line);
      }
    }
    if (!ok || eof) {
      // There was either an error or an EOF condition, so we are done watching
      // the file descriptor.
      progress_controller_.reset();
      return;
    }
  } while (bytes_read);
}

bool PostinstallRunnerAction::ProcessProgressLine(const string& line) {
  double frac = 0;
  if (sscanf(line.c_str(), "global_progress %lf", &frac) == 1 &&
      !std::isnan(frac)) {
    ReportProgress(frac);
    return true;
  }

  return false;
}

void PostinstallRunnerAction::ReportProgress(double frac) {
  if (!delegate_) {
    return;
  }
  if (current_partition_ >= partition_weight_.size() || total_weight_ == 0) {
    delegate_->ProgressUpdate(1.);
    return;
  }
  if (!std::isfinite(frac) || frac < 0) {
    frac = 0;
  }
  if (frac > 1) {
    frac = 1;
  }
  double postinst_action_progress =
      (accumulated_weight_ + partition_weight_[current_partition_] * frac) /
      total_weight_;
  delegate_->ProgressUpdate(postinst_action_progress);
}

void PostinstallRunnerAction::Cleanup() {
  utils::UnmountFilesystem(fs_mount_dir_);
  if (!base::DeleteFile(base::FilePath(fs_mount_dir_))) {
    PLOG(WARNING) << "Not removing temporary mountpoint " << fs_mount_dir_;
  }
  fs_mount_dir_.clear();

  progress_fd_ = -1;
  progress_controller_.reset();

  progress_buffer_.clear();
}

void PostinstallRunnerAction::CompletePartitionPostinstall(
    int return_code, const string& output) {
  current_command_ = 0;
  Cleanup();

  if (return_code != 0) {
    LOG(ERROR) << "Postinst command failed with code: " << return_code;
    ErrorCode error_code = ErrorCode::kPostinstallRunnerError;

    if (return_code == 3) {
      // This special return code means that we tried to update firmware,
      // but couldn't because we booted from FW B, and we need to reboot
      // to get back to FW A.
      error_code = ErrorCode::kPostinstallBootedFromFirmwareB;
    }

    if (return_code == 4) {
      // This special return code means that we tried to update firmware,
      // but couldn't because we booted from FW B, and we need to reboot
      // to get back to FW A.
      error_code = ErrorCode::kPostinstallFirmwareRONotUpdatable;
    }

    // If postinstall script for this partition is optional we can ignore the
    // result.
    if (install_plan_.partitions[current_partition_].postinstall_optional) {
      LOG(INFO) << "Ignoring postinstall failure since it is optional";
    } else {
      return CompletePostinstall(error_code);
    }
  }
  accumulated_weight_ += partition_weight_[current_partition_];
  current_partition_++;
  ReportProgress(0);

  PerformPartitionPostinstall();
}

void PostinstallRunnerAction::CompletePostinstall(ErrorCode error_code) {
  // We only attempt to mark the new slot as active if all the postinstall
  // steps succeeded.
  if (error_code == ErrorCode::kSuccess) {
    if (install_plan_.switch_minios_slot) {
      if (!hardware_->SetActiveMiniOsPartition(
              install_plan_.minios_target_slot)) {
        LOG(ERROR) << "Update completed but unable to change the MiniOS "
                      "active slot to "
                   << install_plan_.minios_target_slot;
      }
    }
    if (install_plan_.switch_slot_on_reboot) {
      if (!boot_control_->GetDynamicPartitionControl()->FinishUpdate(
              install_plan_.powerwash_required) ||
          !boot_control_->SetActiveBootSlot(install_plan_.target_slot)) {
        error_code = ErrorCode::kPostinstallRunnerError;
      } else {
        // Schedules warm reset on next reboot, ignores the error.
        hardware_->SetWarmReset(true);
      }
    } else if (install_plan_.run_post_install) {
      switch (install_plan_.defer_update_action) {
        case DeferUpdateAction::kOff:
          error_code = ErrorCode::kUpdatedButNotActive;
          break;
        case DeferUpdateAction::kHold:
        case DeferUpdateAction::kApplyAndReboot:
        case DeferUpdateAction::kApplyAndShutdown:
          error_code = ErrorCode::kSuccess;
          break;
      }
    }
  }

  ScopedActionCompleter completer(processor_, this);
  completer.set_code(error_code);

  if (error_code != ErrorCode::kSuccess &&
      error_code != ErrorCode::kUpdatedButNotActive) {
    LOG(ERROR) << "Postinstall action failed.";

    // Undo any changes done to trigger Powerwash.
    if (powerwash_scheduled_) {
      hardware_->CancelPowerwash();
    }

    return;
  }

  LOG(INFO) << "All post-install commands succeeded";
  if (HasOutputPipe()) {
    SetOutputObject(install_plan_);
  }
}

void PostinstallRunnerAction::SuspendAction() {
  if (!current_command_) {
    return;
  }
  if (kill(current_command_, SIGSTOP) != 0) {
    PLOG(ERROR) << "Couldn't pause child process " << current_command_;
  } else {
    is_current_command_suspended_ = true;
  }
}

void PostinstallRunnerAction::ResumeAction() {
  if (!current_command_) {
    return;
  }
  if (kill(current_command_, SIGCONT) != 0) {
    PLOG(ERROR) << "Couldn't resume child process " << current_command_;
  } else {
    is_current_command_suspended_ = false;
  }
}

void PostinstallRunnerAction::TerminateProcessing() {
  if (!current_command_) {
    return;
  }
  // Calling KillExec() will discard the callback we registered and therefore
  // the unretained reference to this object.
  Subprocess::Get().KillExec(current_command_);

  // If the command has been suspended, resume it after KillExec() so that the
  // process can process the SIGTERM sent by KillExec().
  if (is_current_command_suspended_) {
    ResumeAction();
  }

  current_command_ = 0;
  Cleanup();
}

}  // namespace chromeos_update_engine
