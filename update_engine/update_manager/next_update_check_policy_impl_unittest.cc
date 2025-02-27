// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/next_update_check_policy_impl.h"

#include <memory>

#include "update_engine/update_manager/policy_test_utils.h"

using base::Time;
using std::string;

namespace chromeos_update_manager {

const NextUpdateCheckPolicyConstants policy_test_constants = {
    // these are specifically NOT the values used by real Policy
    // implementations.
    .timeout_initial_interval = 3 * 60,
    .timeout_periodic_interval = 2 * 60 * 60,
    .timeout_max_backoff_interval = 8 * 60 * 60,
    .timeout_regular_fuzz = 5 * 60,
    .attempt_backoff_max_interval_in_days = 12,
    .attempt_backoff_fuzz_in_hours = 10,
};

class UmNextUpdateCheckTimePolicyImplTest : public UmPolicyTestBase {
 protected:
  UmNextUpdateCheckTimePolicyImplTest() = default;
};

TEST_F(UmNextUpdateCheckTimePolicyImplTest,
       FirstCheckIsAtMostInitialIntervalAfterStart) {
  Time next_update_check;

  // Set the last update time so it'll appear as if this is a first update check
  // in the lifetime of the current updater.
  fake_state_.updater_provider()->var_last_checked_time()->reset(
      new Time(fake_clock_->GetWallclockTime() - base::Minutes(10)));

  CallMethodWithContext(&NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
                        &next_update_check, policy_test_constants);

  EXPECT_LE(fake_clock_->GetWallclockTime(), next_update_check);
  EXPECT_GE(fake_clock_->GetWallclockTime() +
                base::Seconds(policy_test_constants.timeout_initial_interval +
                              policy_test_constants.timeout_regular_fuzz / 2),
            next_update_check);
}

TEST_F(UmNextUpdateCheckTimePolicyImplTest, RecurringCheckBaseIntervalAndFuzz) {
  // Ensure that we're using the correct interval (kPeriodicInterval) and fuzz
  // (ktimeout_regular_fuzz) as base values for period updates.
  Time next_update_check;

  CallMethodWithContext(&NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
                        &next_update_check, policy_test_constants);

  EXPECT_LE(fake_clock_->GetWallclockTime() +
                base::Seconds(policy_test_constants.timeout_periodic_interval -
                              policy_test_constants.timeout_regular_fuzz / 2),
            next_update_check);
  EXPECT_GE(fake_clock_->GetWallclockTime() +
                base::Seconds(policy_test_constants.timeout_periodic_interval +
                              policy_test_constants.timeout_regular_fuzz / 2),
            next_update_check);
}

TEST_F(UmNextUpdateCheckTimePolicyImplTest,
       RecurringCheckBackoffIntervalAndFuzz) {
  // Ensure that we're properly backing off and fuzzing in the presence of
  // failed updates attempts.
  Time next_update_check;

  fake_state_.updater_provider()->var_consecutive_failed_update_checks()->reset(
      new unsigned int{2});

  ExpectStatus(EvalStatus::kSucceeded,
               NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
               &next_update_check, policy_test_constants);

  int expected_interval = policy_test_constants.timeout_periodic_interval * 4;
  EXPECT_LE(fake_clock_->GetWallclockTime() +
                base::Seconds(expected_interval - expected_interval / 2),
            next_update_check);
  EXPECT_GE(fake_clock_->GetWallclockTime() +
                base::Seconds(expected_interval + expected_interval / 2),
            next_update_check);
}

TEST_F(UmNextUpdateCheckTimePolicyImplTest,
       RecurringCheckServerDictatedPollInterval) {
  // Policy honors the server provided check poll interval.
  Time next_update_check;

  const auto kInterval = policy_test_constants.timeout_periodic_interval * 4;
  fake_state_.updater_provider()->var_server_dictated_poll_interval()->reset(
      new unsigned int(kInterval));  // NOLINT(readability/casting)
  // We should not be backing off in this case.
  fake_state_.updater_provider()->var_consecutive_failed_update_checks()->reset(
      new unsigned int(2));  // NOLINT(readability/casting)

  ExpectStatus(EvalStatus::kSucceeded,
               &NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
               &next_update_check, policy_test_constants);

  EXPECT_LE(fake_clock_->GetWallclockTime() +
                base::Seconds(kInterval - kInterval / 2),
            next_update_check);
  EXPECT_GE(fake_clock_->GetWallclockTime() +
                base::Seconds(kInterval + kInterval / 2),
            next_update_check);
}

TEST_F(UmNextUpdateCheckTimePolicyImplTest, ExponentialBackoffIsCapped) {
  Time next_update_check;

  fake_state_.updater_provider()->var_consecutive_failed_update_checks()->reset(
      new unsigned int(100));  // NOLINT(readability/casting)

  ExpectStatus(EvalStatus::kSucceeded,
               &NextUpdateCheckTimePolicyImpl::NextUpdateCheckTime,
               &next_update_check, policy_test_constants);

  EXPECT_LE(
      fake_clock_->GetWallclockTime() +
          base::Seconds(policy_test_constants.timeout_max_backoff_interval -
                        policy_test_constants.timeout_max_backoff_interval / 2),
      next_update_check);
  EXPECT_GE(
      fake_clock_->GetWallclockTime() +
          base::Seconds(policy_test_constants.timeout_max_backoff_interval +
                        policy_test_constants.timeout_max_backoff_interval / 2),
      next_update_check);
}

}  // namespace chromeos_update_manager
