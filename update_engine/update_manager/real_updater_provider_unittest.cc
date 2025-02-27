// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/real_updater_provider.h"

#include <memory>
#include <string>
#include <tuple>

#include <base/time/time.h>
#include <gtest/gtest.h>
#include <update_engine/dbus-constants.h>

#include "update_engine/cros/fake_system_state.h"
#include "update_engine/cros/mock_update_attempter.h"
#include "update_engine/cros/omaha_request_params.h"
#include "update_engine/update_manager/umtest_utils.h"

using base::Time;
using base::TimeDelta;
using chromeos_update_engine::FakePrefs;
using chromeos_update_engine::FakeSystemState;
using chromeos_update_engine::OmahaRequestParams;
using std::string;
using std::unique_ptr;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using update_engine::UpdateAttemptFlags;

namespace {

// Generates a fixed timestamp for use in faking the current time.
Time FixedTime() {
  Time::Exploded now_exp;
  now_exp.year = 2014;
  now_exp.month = 3;
  now_exp.day_of_week = 2;
  now_exp.day_of_month = 18;
  now_exp.hour = 8;
  now_exp.minute = 5;
  now_exp.second = 33;
  now_exp.millisecond = 675;
  Time time;
  std::ignore = Time::FromLocalExploded(now_exp, &time);
  return time;
}

// Rounds down a timestamp to the nearest second. This is useful when faking
// times that are converted to time_t (no sub-second resolution).
Time RoundedToSecond(Time time) {
  Time::Exploded exp;
  time.LocalExplode(&exp);
  exp.millisecond = 0;
  Time rounded_time;
  std::ignore = Time::FromLocalExploded(exp, &rounded_time);
  return rounded_time;
}

ACTION_P(ActionSetUpdateEngineStatusLastCheckedTime, time) {
  arg0->last_checked_time = time;
};

ACTION_P(ActionSetUpdateEngineStatusProgress, progress) {
  arg0->progress = progress;
};

ACTION_P(ActionSetUpdateEngineStatusStatus, status) {
  arg0->status = status;
}

ACTION_P(ActionSetUpdateEngineStatusNewVersion, new_version) {
  arg0->new_version = new_version;
}

ACTION_P(ActionSetUpdateEngineStatusNewSizeBytes, new_size_bytes) {
  arg0->new_size_bytes = new_size_bytes;
}

}  // namespace

namespace chromeos_update_manager {

class UmRealUpdaterProviderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FakeSystemState::CreateInstance();
    provider_.reset(new RealUpdaterProvider());
    // Check that provider initializes correctly.
    ASSERT_TRUE(provider_->Init());
  }

  // Sets up mock expectations for testing the update completed time reporting.
  // |valid| determines whether the returned time is valid. Returns the expected
  // update completed time value.
  Time SetupUpdateCompletedTime(bool valid) {
    const TimeDelta kDurationSinceUpdate = base::Minutes(7);
    const Time kUpdateBootTime = Time() + kDurationSinceUpdate * 2;
    const Time kCurrBootTime = (valid ? kUpdateBootTime + kDurationSinceUpdate
                                      : kUpdateBootTime - kDurationSinceUpdate);
    const Time kCurrWallclockTime = FixedTime();
    EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(),
                GetBootTimeAtUpdate(_))
        .WillOnce(DoAll(SetArgPointee<0>(kUpdateBootTime), Return(true)));
    FakeSystemState::Get()->fake_clock()->SetBootTime(kCurrBootTime);
    FakeSystemState::Get()->fake_clock()->SetWallclockTime(kCurrWallclockTime);
    return kCurrWallclockTime - kDurationSinceUpdate;
  }

  unique_ptr<RealUpdaterProvider> provider_;
};

TEST_F(UmRealUpdaterProviderTest, UpdaterStartedTimeIsWallclockTime) {
  FakeSystemState::Get()->fake_clock()->SetWallclockTime(
      Time::FromSecondsSinceUnixEpoch(123.456));
  FakeSystemState::Get()->fake_clock()->SetMonotonicTime(
      Time::FromSecondsSinceUnixEpoch(456.123));
  // Re-initialize to re-setup the provider under test to use these values.
  provider_.reset(new RealUpdaterProvider());
  ASSERT_TRUE(provider_->Init());
  UmTestUtils::ExpectVariableHasValue(Time::FromSecondsSinceUnixEpoch(123.456),
                                      provider_->var_updater_started_time());
}

TEST_F(UmRealUpdaterProviderTest, GetLastCheckedTimeOkay) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(
          ActionSetUpdateEngineStatusLastCheckedTime(FixedTime().ToTimeT()),
          Return(true)));
  UmTestUtils::ExpectVariableHasValue(RoundedToSecond(FixedTime()),
                                      provider_->var_last_checked_time());
}

TEST_F(UmRealUpdaterProviderTest, GetLastCheckedTimeFailNoValue) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(Return(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_last_checked_time());
}

TEST_F(UmRealUpdaterProviderTest, GetProgressOkayMin) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusProgress(0.0), Return(true)));
  UmTestUtils::ExpectVariableHasValue(0.0, provider_->var_progress());
}

TEST_F(UmRealUpdaterProviderTest, GetProgressOkayMid) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusProgress(0.3), Return(true)));
  UmTestUtils::ExpectVariableHasValue(0.3, provider_->var_progress());
}

TEST_F(UmRealUpdaterProviderTest, GetProgressOkayMax) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusProgress(1.0), Return(true)));
  UmTestUtils::ExpectVariableHasValue(1.0, provider_->var_progress());
}

TEST_F(UmRealUpdaterProviderTest, GetProgressFailNoValue) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(Return(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_progress());
}

TEST_F(UmRealUpdaterProviderTest, GetProgressFailTooSmall) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusProgress(-2.0), Return(true)));
  UmTestUtils::ExpectVariableNotSet(provider_->var_progress());
}

TEST_F(UmRealUpdaterProviderTest, GetProgressFailTooBig) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusProgress(2.0), Return(true)));
  UmTestUtils::ExpectVariableNotSet(provider_->var_progress());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayIdle) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(
          ActionSetUpdateEngineStatusStatus(update_engine::UpdateStatus::IDLE),
          Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kIdle, provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayCheckingForUpdate) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::CHECKING_FOR_UPDATE),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kCheckingForUpdate,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayUpdateAvailable) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::UPDATE_AVAILABLE),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kUpdateAvailable,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayDownloading) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::DOWNLOADING),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kDownloading,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayVerifying) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::VERIFYING),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kVerifying,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayFinalizing) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::FINALIZING),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kFinalizing,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayUpdatedNeedReboot) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::UPDATED_NEED_REBOOT),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kUpdatedNeedReboot,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayReportingErrorEvent) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::REPORTING_ERROR_EVENT),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kReportingErrorEvent,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageOkayAttemptingRollback) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusStatus(
                          update_engine::UpdateStatus::ATTEMPTING_ROLLBACK),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(Stage::kAttemptingRollback,
                                      provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetStageFailNoValue) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(Return(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_stage());
}

TEST_F(UmRealUpdaterProviderTest, GetNewVersionOkay) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(
          DoAll(ActionSetUpdateEngineStatusNewVersion("1.2.0"), Return(true)));
  UmTestUtils::ExpectVariableHasValue(string("1.2.0"),
                                      provider_->var_new_version());
}

TEST_F(UmRealUpdaterProviderTest, GetNewVersionFailNoValue) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(Return(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_new_version());
}

TEST_F(UmRealUpdaterProviderTest, GetPayloadSizeOkayZero) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(
          ActionSetUpdateEngineStatusNewSizeBytes(static_cast<uint64_t>(0)),
          Return(true)));
  UmTestUtils::ExpectVariableHasValue(static_cast<uint64_t>(0),
                                      provider_->var_payload_size());
}

TEST_F(UmRealUpdaterProviderTest, GetPayloadSizeOkayArbitrary) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusNewSizeBytes(
                          static_cast<uint64_t>(567890)),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(static_cast<uint64_t>(567890),
                                      provider_->var_payload_size());
}

TEST_F(UmRealUpdaterProviderTest, GetPayloadSizeOkayTwoGigabytes) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(DoAll(ActionSetUpdateEngineStatusNewSizeBytes(
                          static_cast<uint64_t>(1) << 31),
                      Return(true)));
  UmTestUtils::ExpectVariableHasValue(static_cast<uint64_t>(1) << 31,
                                      provider_->var_payload_size());
}

TEST_F(UmRealUpdaterProviderTest, GetPayloadSizeFailNoValue) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(), GetStatus(_))
      .WillOnce(Return(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_payload_size());
}

TEST_F(UmRealUpdaterProviderTest, GetCurrChannelOkay) {
  const string kChannelName("foo-channel");
  OmahaRequestParams request_params;
  request_params.Init("", "", {});
  request_params.set_current_channel(kChannelName);
  FakeSystemState::Get()->set_request_params(&request_params);
  UmTestUtils::ExpectVariableHasValue(kChannelName,
                                      provider_->var_curr_channel());
}

TEST_F(UmRealUpdaterProviderTest, GetCurrChannelFailEmpty) {
  OmahaRequestParams request_params;
  request_params.Init("", "", {});
  request_params.set_current_channel("");
  FakeSystemState::Get()->set_request_params(&request_params);
  UmTestUtils::ExpectVariableNotSet(provider_->var_curr_channel());
}

TEST_F(UmRealUpdaterProviderTest, GetNewChannelOkay) {
  const string kChannelName("foo-channel");
  OmahaRequestParams request_params;
  request_params.Init("", "", {});
  request_params.set_target_channel(kChannelName);
  FakeSystemState::Get()->set_request_params(&request_params);
  UmTestUtils::ExpectVariableHasValue(kChannelName,
                                      provider_->var_new_channel());
}

TEST_F(UmRealUpdaterProviderTest, GetNewChannelFailEmpty) {
  OmahaRequestParams request_params;
  request_params.Init("", "", {});
  request_params.set_target_channel("");
  FakeSystemState::Get()->set_request_params(&request_params);
  UmTestUtils::ExpectVariableNotSet(provider_->var_new_channel());
}

TEST_F(UmRealUpdaterProviderTest, GetP2PEnabledOkayPrefDoesntExist) {
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_p2p_enabled());
}

TEST_F(UmRealUpdaterProviderTest, GetP2PEnabledOkayPrefReadsFalse) {
  FakeSystemState::Get()->fake_prefs()->SetBoolean(
      chromeos_update_engine::kPrefsP2PEnabled, false);
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_p2p_enabled());
}

TEST_F(UmRealUpdaterProviderTest, GetP2PEnabledReadWhenInitialized) {
  FakeSystemState::Get()->fake_prefs()->SetBoolean(
      chromeos_update_engine::kPrefsP2PEnabled, true);
  provider_.reset(new RealUpdaterProvider());
  ASSERT_TRUE(provider_->Init());
  UmTestUtils::ExpectVariableHasValue(true, provider_->var_p2p_enabled());
}

TEST_F(UmRealUpdaterProviderTest, GetP2PEnabledUpdated) {
  auto* fake_prefs = FakeSystemState::Get()->fake_prefs();
  fake_prefs->SetBoolean(chromeos_update_engine::kPrefsP2PEnabled, false);
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_p2p_enabled());
  fake_prefs->SetBoolean(chromeos_update_engine::kPrefsP2PEnabled, true);
  UmTestUtils::ExpectVariableHasValue(true, provider_->var_p2p_enabled());
  fake_prefs->Delete(chromeos_update_engine::kPrefsP2PEnabled);
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_p2p_enabled());
}

TEST_F(UmRealUpdaterProviderTest, GetCellularEnabledOkayPrefDoesntExist) {
  UmTestUtils::ExpectVariableHasValue(false, provider_->var_cellular_enabled());
}

TEST_F(UmRealUpdaterProviderTest, GetCellularEnabledOkayPrefReadsTrue) {
  FakeSystemState::Get()->fake_prefs()->SetBoolean(
      chromeos_update_engine::kPrefsUpdateOverCellularPermission, true);
  UmTestUtils::ExpectVariableHasValue(true, provider_->var_cellular_enabled());
}

TEST_F(UmRealUpdaterProviderTest, GetMarketSegmentDisabled) {
  auto* fake_prefs = FakeSystemState::Get()->fake_prefs();
  fake_prefs->SetBoolean(chromeos_update_engine::kPrefsMarketSegmentDisabled,
                         true);
  UmTestUtils::ExpectVariableHasValue(true,
                                      provider_->var_market_segment_disabled());
  fake_prefs->SetBoolean(chromeos_update_engine::kPrefsMarketSegmentDisabled,
                         false);
  UmTestUtils::ExpectVariableHasValue(false,
                                      provider_->var_market_segment_disabled());
  fake_prefs->Delete(chromeos_update_engine::kPrefsMarketSegmentDisabled);
  UmTestUtils::ExpectVariableHasValue(false,
                                      provider_->var_market_segment_disabled());
}

TEST_F(UmRealUpdaterProviderTest, GetUpdateCompletedTimeOkay) {
  Time expected = SetupUpdateCompletedTime(true);
  UmTestUtils::ExpectVariableHasValue(expected,
                                      provider_->var_update_completed_time());
}

TEST_F(UmRealUpdaterProviderTest, GetUpdateCompletedTimeFailNoValue) {
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(),
              GetBootTimeAtUpdate(_))
      .WillOnce(Return(false));
  UmTestUtils::ExpectVariableNotSet(provider_->var_update_completed_time());
}

TEST_F(UmRealUpdaterProviderTest, GetUpdateCompletedTimeFailInvalidValue) {
  SetupUpdateCompletedTime(false);
  UmTestUtils::ExpectVariableNotSet(provider_->var_update_completed_time());
}

TEST_F(UmRealUpdaterProviderTest, GetConsecutiveFailedUpdateChecks) {
  const unsigned int kNumFailedChecks = 3;
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(),
              consecutive_failed_update_checks())
      .WillRepeatedly(Return(kNumFailedChecks));
  UmTestUtils::ExpectVariableHasValue(
      kNumFailedChecks, provider_->var_consecutive_failed_update_checks());
}

TEST_F(UmRealUpdaterProviderTest, GetServerDictatedPollInterval) {
  const unsigned int kPollInterval = 2 * 60 * 60;  // Two hours.
  EXPECT_CALL(*FakeSystemState::Get()->mock_update_attempter(),
              server_dictated_poll_interval())
      .WillRepeatedly(Return(kPollInterval));
  UmTestUtils::ExpectVariableHasValue(
      kPollInterval, provider_->var_server_dictated_poll_interval());
}

TEST_F(UmRealUpdaterProviderTest, TestUpdateCheckIntervalTimeout) {
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_test_update_check_interval_timeout());
  auto* fake_prefs = FakeSystemState::Get()->fake_prefs();
  fake_prefs->SetInt64(
      chromeos_update_engine::kPrefsTestUpdateCheckIntervalTimeout, 1);
  UmTestUtils::ExpectVariableHasValue(
      static_cast<int64_t>(1),
      provider_->var_test_update_check_interval_timeout());

  // Make sure the value does not exceed a threshold of 10 minutes.
  fake_prefs->SetInt64(
      chromeos_update_engine::kPrefsTestUpdateCheckIntervalTimeout, 11 * 60);
  // The next 5 reads should return valid values.
  for (int i = 0; i < 5; ++i) {
    UmTestUtils::ExpectVariableHasValue(
        static_cast<int64_t>(10 * 60),
        provider_->var_test_update_check_interval_timeout());
  }

  // Just to make sure it is not cached anywhere and deleted. The variable is
  // allowd to be read 6 times.
  UmTestUtils::ExpectVariableNotSet(
      provider_->var_test_update_check_interval_timeout());
}

}  // namespace chromeos_update_manager
