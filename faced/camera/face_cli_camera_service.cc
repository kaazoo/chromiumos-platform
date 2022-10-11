// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "faced/camera/face_cli_camera_service.h"

#include <memory>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/no_destructor.h>

#include "cros-camera/camera_service_connector.h"

namespace faceauth {

constexpr int kApiVersion = 1;

std::unique_ptr<FaceCliCameraService> FaceCliCameraService::Create(
    base::StringPiece token_path_string) {
  return base::WrapUnique(new FaceCliCameraService(token_path_string));
}

int FaceCliCameraService::Init() {
  base::FilePath token_path(token_path_string_);
  std::string token_string;

  if (!base::ReadFileToString(token_path, &token_string)) {
    LOG(ERROR) << "Failed to read permission token for cros camera service.";
    return 1;
  }

  const cros_cam_init_option_t option = {.api_version = kApiVersion,
                                         .token = token_string.c_str()};

  return cros_cam_init(&option);
}

int FaceCliCameraService::Exit() {
  return cros_cam_exit();
}

int FaceCliCameraService::GetCameraInfo(cros_cam_get_cam_info_cb_t callback,
                                        void* context) {
  return cros_cam_get_cam_info(callback, context);
}

int FaceCliCameraService::StartCapture(
    const cros_cam_capture_request_t* request,
    cros_cam_capture_cb_t callback,
    void* context) {
  return cros_cam_start_capture(request, callback, context);
}

int FaceCliCameraService::StopCapture(int id) {
  return cros_cam_stop_capture(id);
}
}  // namespace faceauth
