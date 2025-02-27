// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/terminator.h"

#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

using testing::ExitedWithCode;

namespace chromeos_update_engine {

class TerminatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Terminator::Init();
    ASSERT_FALSE(Terminator::exit_blocked());
    ASSERT_FALSE(Terminator::exit_requested());
  }
  void TearDown() override {
    // Makes sure subsequent non-Terminator tests don't get accidentally
    // terminated.
    Terminator::Init();
  }
};

typedef TerminatorTest TerminatorDeathTest;

namespace {
void UnblockExitThroughUnblocker() {
  ScopedTerminatorExitUnblocker unblocker = ScopedTerminatorExitUnblocker();
}

void RaiseSIGTERM() {
  ASSERT_EXIT(raise(SIGTERM), ExitedWithCode(2), "");
}
}  // namespace

TEST_F(TerminatorTest, HandleSignalTest) {
  Terminator::set_exit_blocked(true);
  Terminator::HandleSignal(SIGTERM);
  ASSERT_TRUE(Terminator::exit_requested());
}

TEST_F(TerminatorTest, ScopedTerminatorExitUnblockerTest) {
  Terminator::set_exit_blocked(true);
  ASSERT_TRUE(Terminator::exit_blocked());
  ASSERT_FALSE(Terminator::exit_requested());
  UnblockExitThroughUnblocker();
  ASSERT_FALSE(Terminator::exit_blocked());
  ASSERT_FALSE(Terminator::exit_requested());
}

TEST_F(TerminatorDeathTest, ExitTest) {
  ASSERT_EXIT(Terminator::Exit(), ExitedWithCode(2), "");
  Terminator::set_exit_blocked(true);
  ASSERT_EXIT(Terminator::Exit(), ExitedWithCode(2), "");
}

TEST_F(TerminatorDeathTest, RaiseSignalTest) {
  RaiseSIGTERM();
  Terminator::set_exit_blocked(true);
  EXPECT_FATAL_FAILURE(RaiseSIGTERM(), "");
}

TEST_F(TerminatorDeathTest, ScopedTerminatorExitUnblockerExitTest) {
  Terminator::set_exit_blocked(true);
  Terminator::exit_requested_ = 1;
  ASSERT_EXIT(UnblockExitThroughUnblocker(), ExitedWithCode(2), "");
}

}  // namespace chromeos_update_engine
