// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_FAKE_DEVICE_POLICY_PROVIDER_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_FAKE_DEVICE_POLICY_PROVIDER_H_

#include <set>
#include <string>

#include "update_engine/update_manager/device_policy_provider.h"
#include "update_engine/update_manager/fake_variable.h"

namespace chromeos_update_manager {

// Fake implementation of the DevicePolicyProvider base class.
class FakeDevicePolicyProvider : public DevicePolicyProvider {
 public:
  FakeDevicePolicyProvider() {}
  FakeDevicePolicyProvider(const FakeDevicePolicyProvider&) = delete;
  FakeDevicePolicyProvider& operator=(const FakeDevicePolicyProvider&) = delete;

  FakeVariable<bool>* var_device_policy_is_loaded() override {
    return &var_device_policy_is_loaded_;
  }

  FakeVariable<std::string>* var_release_channel() override {
    return &var_release_channel_;
  }

  FakeVariable<bool>* var_release_channel_delegated() override {
    return &var_release_channel_delegated_;
  }

  FakeVariable<std::string>* var_release_lts_tag() override {
    return &var_release_lts_tag_;
  }

  FakeVariable<bool>* var_update_disabled() override {
    return &var_update_disabled_;
  }

  FakeVariable<std::string>* var_target_version_prefix() override {
    return &var_target_version_prefix_;
  }

  FakeVariable<RollbackToTargetVersion>* var_rollback_to_target_version()
      override {
    return &var_rollback_to_target_version_;
  }

  FakeVariable<int>* var_rollback_allowed_milestones() override {
    return &var_rollback_allowed_milestones_;
  }

  FakeVariable<base::TimeDelta>* var_scatter_factor() override {
    return &var_scatter_factor_;
  }

  FakeVariable<std::set<chromeos_update_engine::ConnectionType>>*
  var_allowed_connection_types_for_update() override {
    return &var_allowed_connection_types_for_update_;
  }

  FakeVariable<bool>* var_has_owner() override { return &var_has_owner_; }

  FakeVariable<bool>* var_http_downloads_enabled() override {
    return &var_http_downloads_enabled_;
  }

  FakeVariable<bool>* var_au_p2p_enabled() override {
    return &var_au_p2p_enabled_;
  }

  FakeVariable<bool>* var_allow_kiosk_app_control_chrome_version() override {
    return &var_allow_kiosk_app_control_chrome_version_;
  }

  FakeVariable<WeeklyTimeIntervalVector>* var_disallowed_time_intervals()
      override {
    return &var_disallowed_time_intervals_;
  }

  FakeVariable<ChannelDowngradeBehavior>* var_channel_downgrade_behavior()
      override {
    return &var_channel_downgrade_behavior_;
  }

  FakeVariable<base::Version>* var_device_minimum_version() override {
    return &var_device_minimum_version_;
  }

  FakeVariable<std::string>* var_quick_fix_build_token() override {
    return &var_quick_fix_build_token_;
  }

  FakeVariable<std::string>* var_market_segment() override {
    return &var_market_segment_;
  }

  FakeVariable<bool>* var_is_enterprise_enrolled() override {
    return &var_is_enterprise_enrolled_;
  };

 private:
  FakeVariable<bool> var_device_policy_is_loaded_{"device_policy_is_loaded",
                                                  kVariableModePoll};
  FakeVariable<std::string> var_release_channel_{"release_channel",
                                                 kVariableModePoll};
  FakeVariable<bool> var_release_channel_delegated_{"release_channel_delegated",
                                                    kVariableModePoll};
  FakeVariable<std::string> var_release_lts_tag_{"release_lts_tag",
                                                 kVariableModePoll};
  FakeVariable<bool> var_update_disabled_{"update_disabled",
                                          kVariableModeAsync};
  FakeVariable<std::string> var_target_version_prefix_{"target_version_prefix",
                                                       kVariableModePoll};
  FakeVariable<RollbackToTargetVersion> var_rollback_to_target_version_{
      "rollback_to_target_version", kVariableModePoll};
  FakeVariable<int> var_rollback_allowed_milestones_{
      "rollback_allowed_milestones", kVariableModePoll};
  FakeVariable<base::TimeDelta> var_scatter_factor_{"scatter_factor",
                                                    kVariableModePoll};
  FakeVariable<std::set<chromeos_update_engine::ConnectionType>>
      var_allowed_connection_types_for_update_{
          "allowed_connection_types_for_update", kVariableModePoll};
  FakeVariable<bool> var_has_owner_{"owner", kVariableModePoll};
  FakeVariable<bool> var_http_downloads_enabled_{"http_downloads_enabled",
                                                 kVariableModePoll};
  FakeVariable<bool> var_au_p2p_enabled_{"au_p2p_enabled", kVariableModePoll};
  FakeVariable<bool> var_allow_kiosk_app_control_chrome_version_{
      "allow_kiosk_app_control_chrome_version", kVariableModePoll};
  FakeVariable<WeeklyTimeIntervalVector> var_disallowed_time_intervals_{
      "disallowed_time_intervals", kVariableModeAsync};
  FakeVariable<ChannelDowngradeBehavior> var_channel_downgrade_behavior_{
      "channel_downgrade_behavior", kVariableModePoll};
  FakeVariable<base::Version> var_device_minimum_version_{
      "device_minimum_version", kVariableModePoll};
  FakeVariable<std::string> var_quick_fix_build_token_{"quick_fix_build_token",
                                                       kVariableModePoll};
  FakeVariable<std::string> var_market_segment_{"market_segment",
                                                kVariableModePoll};
  FakeVariable<bool> var_is_enterprise_enrolled_{"is_enterprise_enrolled",
                                                 kVariableModeAsync};
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_FAKE_DEVICE_POLICY_PROVIDER_H_
