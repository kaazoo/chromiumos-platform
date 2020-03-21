/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/libcamera_connector/camera_client.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/posix/safe_strerror.h>
#include <drm_fourcc.h>

#include "common/libcamera_connector/camera_metadata_utils.h"
#include "common/libcamera_connector/supported_formats.h"
#include "common/libcamera_connector/types.h"
#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"

namespace {

std::string GetCameraName(const cros::mojom::CameraInfoPtr& info) {
  switch (info->facing) {
    case cros::mojom::CameraFacing::CAMERA_FACING_BACK:
      return "Back Camera";
    case cros::mojom::CameraFacing::CAMERA_FACING_FRONT:
      return "Front Camera";
    case cros::mojom::CameraFacing::CAMERA_FACING_EXTERNAL:
      return "External Camera";
    default:
      return "Unknown Camera";
  }
}

}  // namespace

namespace cros {

CameraClient::CameraClient()
    : ipc_thread_("CamClient"),
      camera_hal_client_(this),
      cam_info_callback_(nullptr),
      capture_started_(false) {}

void CameraClient::Init(RegisterClientCallback register_client_callback,
                        IntOnceCallback init_callback) {
  bool ret = ipc_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
  if (!ret) {
    LOGF(ERROR) << "Failed to start IPC thread";
    std::move(init_callback).Run(-ENODEV);
    return;
  }
  std::set<cros_cam_device_t> active_devices_;
  init_callback_ = std::move(init_callback);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::RegisterClient, base::Unretained(this),
                     std::move(register_client_callback)));
}

void CameraClient::Exit() {
  VLOGF_ENTER();
  {
    base::AutoLock l(capture_started_lock_);
    if (capture_started_) {
      auto future = cros::Future<int>::Create(nullptr);
      stop_callback_ = cros::GetFutureCallback(future);
      client_ops_.StopCapture(
          base::Bind(&CameraClient::OnClosedDevice, base::Unretained(this)));
      if (future->Get() != 0) {
        LOGF(ERROR) << "Failed to close device";
      }
    }
  }

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::CloseOnThread, base::Unretained(this)));
  ipc_thread_.Stop();
}

void CameraClient::SetUpChannel(mojom::CameraModulePtr camera_module) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  LOGF(INFO) << "Received camera module from camera HAL dispatcher";
  camera_module_ = std::move(camera_module);

  GetNumberOfCameras();
}

int CameraClient::SetCameraInfoCallback(cros_cam_get_cam_info_cb_t callback,
                                        void* context) {
  VLOGF_ENTER();

  cam_info_callback_ = callback;
  cam_info_context_ = context;

  SendCameraInfo();
  return 0;
}

int CameraClient::StartCapture(cros_cam_device_t id,
                               const cros_cam_format_info_t* format,
                               cros_cam_capture_cb_t callback,
                               void* context) {
  VLOGF_ENTER();
  if (!IsDeviceActive(id)) {
    LOGF(ERROR) << "Cannot start capture on an inactive device";
    return -ENODEV;
  }

  LOGF(INFO) << "Starting capture";

  // TODO(b/151047930): Check whether this format info is actually supported.
  request_camera_id_ = *reinterpret_cast<int32_t*>(id);
  request_format_ = *format;
  request_callback_ = callback;
  request_context_ = context;

  // TODO(b/151047930): Support other formats.
  CHECK_EQ(request_format_.fourcc, DRM_FORMAT_R8);

  auto future = cros::Future<int>::Create(nullptr);
  start_callback_ = cros::GetFutureCallback(future);
  base::AutoLock l(capture_started_lock_);
  if (capture_started_) {
    LOGF(WARNING) << "Capture already started";
    return -EINVAL;
  }

  client_ops_.Init(base::BindOnce(&CameraClient::OnDeviceOpsReceived,
                                  base::Unretained(this)));

  return future->Get();
}

void CameraClient::StopCapture(cros_cam_device_t id) {
  VLOGF_ENTER();
  if (!IsDeviceActive(id)) {
    LOGF(ERROR) << "Cannot stop capture on an inactive device";
    return;
  }

  LOGF(INFO) << "Stopping capture";

  int32_t camera_id = *reinterpret_cast<int32_t*>(id);
  // TODO(lnishan): Support multi-device streaming.
  CHECK_EQ(request_camera_id_, camera_id);

  base::AutoLock l(capture_started_lock_);
  if (!capture_started_) {
    LOGF(WARNING) << "Capture already stopped";
    return;
  }

  auto future = cros::Future<int>::Create(nullptr);
  stop_callback_ = cros::GetFutureCallback(future);
  client_ops_.StopCapture(
      base::Bind(&CameraClient::OnClosedDevice, base::Unretained(this)));
  if (future->Get() != 0) {
    LOGF(ERROR) << "Failed to close device";
  }
}

void CameraClient::RegisterClient(
    RegisterClientCallback register_client_callback) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  mojom::CameraHalClientPtr client_ptr;
  camera_hal_client_.Bind(mojo::MakeRequest(&client_ptr));
  std::move(register_client_callback).Run(std::move(client_ptr));
}

void CameraClient::CloseOnThread() {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_hal_client_.Close();
}

void CameraClient::GetNumberOfCameras() {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_module_->GetNumberOfCameras(
      base::Bind(&CameraClient::OnGotNumberOfCameras, base::Unretained(this)));
}

void CameraClient::OnGotNumberOfCameras(int32_t num_builtin_cameras) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  num_builtin_cameras_ = num_builtin_cameras;
  LOGF(INFO) << "Number of builtin cameras: " << num_builtin_cameras_;

  for (int32_t i = 0; i < num_builtin_cameras_; ++i) {
    camera_id_list_.push_back(i);
    active_devices_.insert(&camera_id_list_.back());
  }
  if (num_builtin_cameras_ == 0) {
    std::move(init_callback_).Run(0);
    return;
  }
  camera_id_iter_ = camera_id_list_.begin();
  GetCameraInfo(*camera_id_iter_);
}

void CameraClient::GetCameraInfo(int32_t camera_id) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_module_->GetCameraInfo(
      camera_id,
      base::Bind(&CameraClient::OnGotCameraInfo, base::Unretained(this)));
}

void CameraClient::OnGotCameraInfo(int32_t result, mojom::CameraInfoPtr info) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  int32_t camera_id = *camera_id_iter_;
  if (result != 0) {
    LOGF(ERROR) << "Failed to get camera info of " << camera_id << ": "
                << base::safe_strerror(-result);
    std::move(init_callback_).Run(-ENODEV);
    return;
  }

  LOGF(INFO) << "Gotten camera info of " << camera_id;

  auto& camera_info = camera_info_map_[camera_id];
  camera_info.name = GetCameraName(info);

  auto& format_info = camera_info_map_[camera_id].format_info;
  auto min_frame_durations = GetMetadataEntryAsSpan<int64_t>(
      info->static_camera_characteristics,
      mojom::CameraMetadataTag::ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
  for (size_t i = 0; i < min_frame_durations.size(); i += 4) {
    uint32_t drm_format = ResolveDrmFormat(min_frame_durations[i + 0]);
    if (drm_format == 0) {  // Failed to resolve to a format
      LOGF(WARNING) << "Failed to resolve to a DRM format for "
                    << min_frame_durations[i + 0];
      continue;
    }
    cros_cam_format_info_t info = {
        .fourcc = drm_format,
        .width = static_cast<unsigned>(min_frame_durations[i + 1]),
        .height = static_cast<unsigned>(min_frame_durations[i + 2]),
        .fps = static_cast<unsigned>(round(1e9 / min_frame_durations[i + 3]))};
    format_info.push_back(std::move(info));
  }

  camera_info.jpeg_max_size = GetMetadataEntryAsSpan<int32_t>(
      info->static_camera_characteristics,
      mojom::CameraMetadataTag::ANDROID_JPEG_MAX_SIZE)[0];

  ++camera_id_iter_;
  if (camera_id_iter_ == camera_id_list_.end()) {
    std::move(init_callback_).Run(0);
  } else {
    GetCameraInfo(*camera_id_iter_);
  }
}

void CameraClient::SendCameraInfo() {
  VLOGF_ENTER();

  for (auto& camera_id : camera_id_list_) {
    auto it = camera_info_map_.find(camera_id);
    if (camera_info_map_.find(camera_id) == camera_info_map_.end()) {
      LOGF(ERROR) << "Cannot find the info of camera " << camera_id;
      continue;
    }
    cros_cam_info_t cam_info = {
        .id = reinterpret_cast<void*>(&camera_id),
        .name = it->second.name.c_str(),
        .format_count = static_cast<unsigned>(it->second.format_info.size()),
        .format_info = it->second.format_info.data()};

    int ret =
        (*cam_info_callback_)(cam_info_context_, &cam_info, /*is_removed=*/0);
    if (ret != 0) {
      // Deregister callback
      cam_info_callback_ = nullptr;
      cam_info_context_ = nullptr;
      break;
    }
  }
}

void CameraClient::OnDeviceOpsReceived(
    mojom::Camera3DeviceOpsRequest device_ops_request) {
  VLOGF_ENTER();
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraClient::OpenDeviceOnThread, base::Unretained(this),
                     std::move(device_ops_request)));
}

void CameraClient::OpenDeviceOnThread(
    mojom::Camera3DeviceOpsRequest device_ops_request) {
  VLOGF_ENTER();
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());

  camera_module_->OpenDevice(
      request_camera_id_, std::move(device_ops_request),
      base::Bind(&CameraClient::OnOpenedDevice, base::Unretained(this)));
}

void CameraClient::OnOpenedDevice(int32_t result) {
  if (result != 0) {
    LOGF(ERROR) << "Failed to open camera " << request_camera_id_;
  } else {
    LOGF(INFO) << "Camera opened successfully";
    client_ops_.StartCapture(
        request_camera_id_, &request_format_, request_callback_,
        request_context_, camera_info_map_[request_camera_id_].jpeg_max_size);
    // Caller should hold the |capture_started_lock_| until the device is
    // opened.
    CHECK(!capture_started_lock_.Try());
    capture_started_ = true;
  }
  std::move(start_callback_).Run(result);
}

void CameraClient::OnClosedDevice(int32_t result) {
  if (result != 0) {
    LOGF(ERROR) << "Failed to close camera " << request_camera_id_;
  } else {
    LOGF(INFO) << "Camera closed successfully";
  }
  // Caller should hold the |capture_started_lock_| until the device is closed.
  CHECK(!capture_started_lock_.Try());
  // Capture is marked stopped regardless of the result. When an error takes
  // place, we don't want to close or use the camera again.
  capture_started_ = false;
  std::move(stop_callback_).Run(result);
}

bool CameraClient::IsDeviceActive(cros_cam_device_t device) {
  return active_devices_.find(device) != active_devices_.end();
}

}  // namespace cros
