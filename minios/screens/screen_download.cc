// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_download.h"

#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <minios/proto_bindings/minios.pb.h>

#include "minios/log_store_manager.h"
#include "minios/process_manager.h"
#include "minios/screen_controller.h"
#include "minios/utils.h"

namespace minios {

ScreenDownload::ScreenDownload(
    std::unique_ptr<RecoveryInstallerInterface> recovery_installer,
    std::shared_ptr<UpdateEngineProxy> update_engine_proxy,
    std::shared_ptr<DrawInterface> draw_utils,
    std::unique_ptr<MetricsReporterInterface> metrics_reporter,
    std::shared_ptr<LogStoreManagerInterface> log_store_manager,
    std::shared_ptr<ProcessManagerInterface> process_manager,
    ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3,
          /*index_=*/1,
          State::RECOVERING,
          draw_utils,
          screen_controller),
      recovery_installer_(std::move(recovery_installer)),
      update_engine_proxy_(update_engine_proxy),
      display_update_engine_state_(false),
      metrics_reporter_(std::move(metrics_reporter)),
      log_store_manager_(log_store_manager),
      process_manager_(process_manager) {
  update_engine_proxy_->SetDelegate(this);
}

void ScreenDownload::Show() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructionsWithTitle("MiniOS_downloading");
  draw_utils_->ShowStepper({"done", "done", "3-done"});
  draw_utils_->ShowProgressBar();
  StartRecovery();
  SetState(State::RECOVERING);
}

void ScreenDownload::Finalizing() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructionsWithTitle("MiniOS_finalizing");
  draw_utils_->ShowStepper({"done", "done", "3-done"});
  draw_utils_->ShowIndeterminateProgressBar();
  SetState(State::FINALIZING);
}

void ScreenDownload::Completed() {
  draw_utils_->HideIndeterminateProgressBar();
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructions("title_MiniOS_complete");
  draw_utils_->ShowStepper({"done", "done", "done"});
  if (MountStatefulPartition(process_manager_)) {
    metrics_reporter_->ReportNBRComplete();
    if (log_store_manager_) {
      if (!base::CreateDirectory(log_store_path_)) {
        LOG(ERROR) << "Failed to setup log directory=" << log_store_path_;
      } else if (const auto dest_path =
                     log_store_path_.Append(std::string{kLogArchiveFile});
                 !log_store_manager_->SaveLogs(
                     LogStoreManager::LogDirection::Stateful, dest_path)) {
        LOG(ERROR) << "Failed to save logs to=" << dest_path;
      }
    }
    if (!UnmountStatefulPartition(process_manager_)) {
      LOG(WARNING) << "Failed to unmount stateful partition";
    }
  } else {
    LOG(WARNING) << "Failed to mount stateful, unable to report metrics.";
  }
  SetState(State::COMPLETED);
  update_engine_proxy_->TriggerReboot();
}

void ScreenDownload::ShowButtons() {}

void ScreenDownload::OnKeyPress(int key_changed) {}

void ScreenDownload::OnProgressChanged(
    const update_engine::StatusResult& status) {
  // Only make UI changes when needed to prevent unnecessary screen changes.
  if (!display_update_engine_state_) {
    return;
  }

  // Only reshow base screen if moving to a new update stage. This prevents
  // flickering as the screen repaints.
  update_engine::Operation operation = status.current_operation();
  switch (operation) {
    case update_engine::Operation::DOWNLOADING:
      if (previous_update_state_ != operation) {
        Show();
      }
      draw_utils_->ShowProgressPercentage(status.progress());
      break;
    case update_engine::Operation::FINALIZING:
      if (previous_update_state_ != operation) {
        LOG(INFO) << "Finalizing installation please wait.";
        Finalizing();
      }
      break;
    case update_engine::Operation::UPDATED_NEED_REBOOT:
      Completed();
      // Don't make any more updates to the UI.
      display_update_engine_state_ = false;
      break;
    case update_engine::Operation::REPORTING_ERROR_EVENT:
    case update_engine::Operation::DISABLED:
    case update_engine::Operation::ERROR:
      LOG(ERROR) << "Could not finish the installation, failed with status: "
                 << status.current_operation();
      screen_controller_->OnError(ScreenType::kDownloadError);
      display_update_engine_state_ = false;
      if (log_store_manager_) {
        log_store_manager_->SaveLogs(LogStoreManager::LogDirection::Disk);
      }
      break;
    default:
      // Only `IDLE` can go back to `IDLE` without an error.
      // Otherwise there will be an indefinite hang during screens.
      if (previous_update_state_ != update_engine::Operation::IDLE &&
          operation == update_engine::Operation::IDLE) {
        LOG(WARNING) << "Update engine went from " << previous_update_state_
                     << " back to IDLE.";
        screen_controller_->OnError(ScreenType::kDownloadError);
        display_update_engine_state_ = false;
      }
      break;
  }
  previous_update_state_ = operation;
}

void ScreenDownload::Reset() {
  index_ = 1;
  draw_utils_->HideIndeterminateProgressBar();
}

ScreenType ScreenDownload::GetType() {
  return ScreenType::kStartDownload;
}

std::string ScreenDownload::GetName() {
  return "ScreenDownload";
}

void ScreenDownload::StartRecovery() {
  metrics_reporter_->RecordNBRStart();

  if (!recovery_installer_->RepartitionDisk()) {
    LOG(ERROR) << "Could not repartition disk. Unable to continue.";
    screen_controller_->OnError(ScreenType::kGeneralError);
    return;
  }

  if (!update_engine_proxy_->StartUpdate()) {
    LOG(ERROR) << "Could not start update. Unable to continue.";
    screen_controller_->OnError(ScreenType::kDownloadError);
    return;
  }

  display_update_engine_state_ = true;
}

}  // namespace minios
