// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_status_utils.h"

#include <string>

#include <gtest/gtest.h>

using std::string;

namespace chromeos_update_engine {

TEST(UpdateStatusUtilsTest, UpdateEngineStatusToStringTest) {
  // Keep field assignments in same order as they were declared,
  // to prevent compiler warning, -Wreorder-init-fields.
  update_engine::UpdateEngineStatus update_engine_status = {
      .last_checked_time = 156000000,
      .status = update_engine::UpdateStatus::CHECKING_FOR_UPDATE,
      .progress = 0.5,
      .new_size_bytes = 888,
      .new_version = "12345.0.0",
      .is_enterprise_rollback = true,
      .is_install = true,
      .will_powerwash_after_reboot = true,
      .last_attempt_error = 0,
      .is_interactive = true,
      .will_defer_update = true,
  };
  string print =
      R"(CURRENT_OP=UPDATE_STATUS_CHECKING_FOR_UPDATE
IS_ENTERPRISE_ROLLBACK=true
IS_INSTALL=true
IS_INTERACTIVE=true
LAST_ATTEMPT_ERROR=ErrorCode::kSuccess
LAST_CHECKED_TIME=156000000
NEW_SIZE=888
NEW_VERSION=12345.0.0
PROGRESS=0.5
WILL_DEFER_UPDATE=true
WILL_POWERWASH_AFTER_REBOOT=true
)";
  EXPECT_EQ(print, UpdateEngineStatusToString(update_engine_status));
}

}  // namespace chromeos_update_engine
