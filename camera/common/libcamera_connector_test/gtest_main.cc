/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include <base/command_line.h>
#include <base/synchronization/waitable_event.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>
#include <linux/videodev2.h>

#include "cros-camera/camera_service_connector.h"
#include "cros-camera/common.h"

namespace cros {
namespace tests {

namespace {

constexpr auto kDefaultTimeout = base::TimeDelta::FromSeconds(5);

std::string FourccToString(uint32_t fourcc) {
  std::string result = "0000";
  for (int i = 0; i < 4; i++) {
    auto c = static_cast<char>(fourcc & 0xFF);
    if (c <= 0x1f || c >= 0x7f) {
      return base::StringPrintf("%#x", fourcc);
    }
    result[i] = c;
    fourcc >>= 8;
  }
  return result;
}

std::string CameraFormatInfoToString(const cros_cam_format_info_t& info) {
  return base::StringPrintf("%s %4ux%4u %3ufps",
                            FourccToString(info.fourcc).c_str(), info.width,
                            info.height, info.fps);
}

bool IsSameFormat(const cros_cam_format_info_t& fmt1,
                  const cros_cam_format_info_t& fmt2) {
  return fmt1.fourcc == fmt2.fourcc && fmt1.width == fmt2.width &&
         fmt1.height == fmt2.height && fmt1.fps == fmt2.fps;
}

}  // namespace

class ConnectorEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    ASSERT_EQ(cros_cam_init(), 0);
    LOGF(INFO) << "Camera connector initialized";
  }

  void TearDown() override {
    cros_cam_exit();
    LOGF(INFO) << "Camera connector exited";
  }
};

class FrameCapturer {
 public:
  FrameCapturer& SetNumFrames(int num_frames) {
    num_frames_ = num_frames;
    return *this;
  }

  FrameCapturer& SetDuration(base::TimeDelta duration) {
    duration_ = duration;
    return *this;
  }

  int Run(cros_cam_device_t id, cros_cam_format_info_t format) {
    num_frames_captured_ = 0;
    capture_done_.Reset();

    if (cros_cam_start_capture(id, &format, &FrameCapturer::CaptureCallback,
                               this) != 0) {
      ADD_FAILURE() << "failed to start capture";
      return 0;
    }

    // Wait until |duration_| passed or |num_frames_| captured.
    capture_done_.TimedWait(duration_);

    cros_cam_stop_capture(id);
    if (!capture_done_.IsSignaled()) {
      capture_done_.Signal();
    }

    LOGF(INFO) << "Captured " << num_frames_captured_ << " frames";
    return num_frames_captured_;
  }

 private:
  // non-zero return value should stop the capture.
  int GotFrame(const cros_cam_frame_t* frame) {
    if (capture_done_.IsSignaled()) {
      ADD_FAILURE() << "got frame after capture is done";
      return -1;
    }

    num_frames_captured_++;
    if (num_frames_captured_ == num_frames_) {
      capture_done_.Signal();
      return -1;
    }

    // TODO(b/151047930): Verify the content of frame.

    return 0;
  }

  static int CaptureCallback(void* context, const cros_cam_frame_t* frame) {
    auto* self = reinterpret_cast<FrameCapturer*>(context);
    return self->GotFrame(frame);
  }

  int num_frames_ = INT_MAX;
  base::TimeDelta duration_ = kDefaultTimeout;

  int num_frames_captured_;
  base::WaitableEvent capture_done_;
};

class CameraClient {
 public:
  void ProbeCameraInfo() {
    ASSERT_EQ(cros_cam_get_cam_info(&CameraClient::GetCamInfoCallback, this),
              0);
    EXPECT_GT(camera_infos_.size(), 0) << "no camera found";
    // All connected cameras should be already reported by the callback
    // function, set the frozen flag to capture unexpected hotplug events
    // during test. Please see the comment of cros_cam_get_cam_info() for more
    // details.
    camera_info_frozen_ = true;
  }

  void DumpCameraInfo() {
    for (const auto& info : camera_infos_) {
      LOGF(INFO) << "id: " << info.id;
      LOGF(INFO) << "name: " << info.name;
      LOGF(INFO) << "format_count: " << info.format_count;
      for (int i = 0; i < info.format_count; i++) {
        LOGF(INFO) << base::StringPrintf(
            "Format %2d: %s", i,
            CameraFormatInfoToString(info.format_info[i]).c_str());
      }
    }
  }

  cros_cam_device_t FindIdForFormat(const cros_cam_format_info_t& format) {
    for (const auto& info : camera_infos_) {
      for (int i = 0; i < info.format_count; i++) {
        if (IsSameFormat(format, info.format_info[i])) {
          return info.id;
        }
      }
    }
    return nullptr;
  }

 private:
  int GotCameraInfo(const cros_cam_info_t* info, unsigned is_removed) {
    EXPECT_FALSE(camera_info_frozen_) << "unexpected hotplug events";
    EXPECT_EQ(is_removed, 0) << "unexpected removing events";
    EXPECT_GT(info->format_count, 0) << "no available formats";
    camera_infos_.push_back(*info);
    LOGF(INFO) << "Got camera info for id: " << info->id;
    return 0;
  }

  static int GetCamInfoCallback(void* context,
                                const cros_cam_info_t* info,
                                unsigned is_removed) {
    auto* self = reinterpret_cast<CameraClient*>(context);
    return self->GotCameraInfo(info, is_removed);
  }
  std::vector<cros_cam_info_t> camera_infos_;
  bool camera_info_frozen_ = false;
};

class CaptureTest
    : public ::testing::Test,
      public ::testing::WithParamInterface<cros_cam_format_info_t> {
 protected:
  void SetUp() override {
    client_.ProbeCameraInfo();
    format_ = GetParam();
    camera_id_ = client_.FindIdForFormat(format_);
    ASSERT_NE(camera_id_, nullptr);
  }

  CameraClient client_;
  FrameCapturer capturer_;

  cros_cam_device_t camera_id_;
  cros_cam_format_info_t format_;
};

TEST(ConnectorTest, GetInfo) {
  CameraClient client;
  client.ProbeCameraInfo();
  client.DumpCameraInfo();
}

TEST_P(CaptureTest, OneFrame) {
  int num_frames_captured = capturer_.SetNumFrames(1).Run(camera_id_, format_);
  EXPECT_EQ(num_frames_captured, 1);
}

TEST_P(CaptureTest, ThreeSeconds) {
  const auto kDuration = base::TimeDelta::FromSeconds(3);
  int num_frames_captured =
      capturer_.SetDuration(kDuration).Run(camera_id_, format_);
  // It's expected to get more than 1 frame in 3s.
  EXPECT_GT(num_frames_captured, 1);
}

// These should be supported on all devices.
constexpr cros_cam_format_info_t kTestFormats[] = {
    {V4L2_PIX_FMT_NV12, 640, 480, 30},
    {V4L2_PIX_FMT_MJPEG, 640, 480, 30},
};

INSTANTIATE_TEST_SUITE_P(ConnectorTest,
                         CaptureTest,
                         ::testing::ValuesIn(kTestFormats),
                         [](const auto& info) {
                           const cros_cam_format_info_t& fmt = info.param;
                           return base::StringPrintf(
                               "%s_%ux%u_%ufps",
                               FourccToString(fmt.fourcc).c_str(), fmt.width,
                               fmt.height, fmt.fps);
                         });

}  // namespace tests
}  // namespace cros

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  logging::SetLogItems(/*enable_process_id=*/true, /*enable_thread_id=*/true,
                       /*enable_timestamp=*/true, /*enable_tickcount=*/false);

  ::testing::AddGlobalTestEnvironment(new cros::tests::ConnectorEnvironment());
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
