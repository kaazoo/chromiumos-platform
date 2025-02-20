// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_DYNAMIC_PARTITION_CONTROL_INTERFACE_H_
#define UPDATE_ENGINE_COMMON_DYNAMIC_PARTITION_CONTROL_INTERFACE_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "update_engine/common/action.h"
#include "update_engine/common/cleanup_previous_update_action_delegate.h"
#include "update_engine/common/error_code.h"
#include "update_engine/common/prefs_interface.h"
#include "update_engine/update_metadata.pb.h"

namespace chromeos_update_engine {

struct FeatureFlag {
  enum class Value { NONE = 0, RETROFIT, LAUNCH };
  constexpr explicit FeatureFlag(Value value) : value_(value) {}
  constexpr bool IsEnabled() const { return value_ != Value::NONE; }
  constexpr bool IsRetrofit() const { return value_ == Value::RETROFIT; }
  constexpr bool IsLaunch() const { return value_ == Value::LAUNCH; }

 private:
  Value value_;
};

class BootControlInterface;

class DynamicPartitionControlInterface {
 public:
  virtual ~DynamicPartitionControlInterface() = default;

  // Return the feature flags of dynamic partitions on this device.
  // Return RETROFIT iff dynamic partitions is retrofitted on this device,
  //        LAUNCH iff this device is launched with dynamic partitions,
  //        NONE iff dynamic partitions is disabled on this device.
  virtual FeatureFlag GetDynamicPartitionsFeatureFlag() = 0;

  // Return the feature flags of Virtual A/B on this device.
  virtual FeatureFlag GetVirtualAbFeatureFlag() = 0;

  // Attempt to optimize |operation|.
  // If successful, |optimized| contains an operation with extents that
  // needs to be written.
  // If failed, no optimization is available, and caller should perform
  // |operation| directly.
  // |partition_name| should not have the slot suffix; implementation of
  // DynamicPartitionControlInterface checks partition at the target slot
  // previously set with PreparePartitionsForUpdate().
  virtual bool OptimizeOperation(const std::string& partition_name,
                                 const InstallOperation& operation,
                                 InstallOperation* optimized) = 0;

  // Do necessary cleanups before destroying the object.
  virtual void Cleanup() = 0;

  // Prepare all partitions for an update specified in |manifest|.
  // This is needed before calling MapPartitionOnDeviceMapper(), otherwise the
  // device would be mapped in an inconsistent way.
  // If |update| is set, create snapshots and writes super partition metadata.
  // If |required_size| is not null and call fails due to insufficient space,
  // |required_size| will be set to total free space required on userdata
  // partition to apply the update. Otherwise (call succeeds, or fails
  // due to other errors), |required_size| is set to zero.
  virtual bool PreparePartitionsForUpdate(uint32_t source_slot,
                                          uint32_t target_slot,
                                          const DeltaArchiveManifest& manifest,
                                          bool update,
                                          uint64_t* required_size) = 0;

  // After writing to new partitions, before rebooting into the new slot, call
  // this function to indicate writes to new partitions are done.
  virtual bool FinishUpdate(bool powerwash_required) = 0;

  // Get an action to clean up previous update.
  // Return NoOpAction on non-Virtual A/B devices.
  // Before applying the next update, run this action to clean up previous
  // update files. This function blocks until delta files are merged into
  // current OS partitions and finished cleaning up.
  // - If successful, action completes with kSuccess.
  // - If any error, but caller should retry after reboot, action completes with
  //   kError.
  // - If any irrecoverable failures, action completes with kDeviceCorrupted.
  //
  // See ResetUpdate for differences between CleanuPreviousUpdateAction and
  // ResetUpdate.
  virtual std::unique_ptr<AbstractAction> GetCleanupPreviousUpdateAction(
      BootControlInterface* boot_control,
      PrefsInterface* prefs,
      CleanupPreviousUpdateActionDelegateInterface* delegate) = 0;

  // Called after an unwanted payload has been successfully applied and the
  // device has not yet been rebooted.
  //
  // For snapshot updates (Virtual A/B), it calls
  // DeltaPerformer::ResetUpdateProgress(false /* quick */) and
  // frees previously allocated space; the next update will need to be
  // started over.
  //
  // Note: CleanupPreviousUpdateAction does not do anything if an update is in
  // progress, while ResetUpdate() forcefully free previously
  // allocated space for snapshot updates.
  virtual bool ResetUpdate(PrefsInterface* prefs) = 0;

  // Reads the dynamic partitions metadata from the current slot, and puts the
  // name of the dynamic partitions with the current suffix to |partitions|.
  // Returns true on success.
  virtual bool ListDynamicPartitionsForSlot(
      uint32_t current_slot, std::vector<std::string>* partitions) = 0;

  // Finds a possible location that list all block devices by name; and puts
  // the result in |path|. Returns true on success.
  // Sample result: /dev/block/by-name/
  virtual bool GetDeviceDir(std::string* path) = 0;

  // Verifies that the untouched dynamic partitions in the target metadata have
  // the same extents as the source metadata.
  virtual bool VerifyExtentsForUntouchedPartitions(
      uint32_t source_slot,
      uint32_t target_slot,
      const std::vector<std::string>& partitions) = 0;
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_DYNAMIC_PARTITION_CONTROL_INTERFACE_H_
