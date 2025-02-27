// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/update_manager/boxed_value.h"

#include <stdint.h>

#include <set>
#include <string>

#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <base/version.h>

#include "update_engine/common/connection_utils.h"
#include "update_engine/common/utils.h"
#include "update_engine/update_manager/rollback_prefs.h"
#include "update_engine/update_manager/shill_provider.h"
#include "update_engine/update_manager/updater_provider.h"
#include "update_engine/update_manager/weekly_time.h"

using chromeos_update_engine::ConnectionType;
using chromeos_update_engine::connection_utils::StringForConnectionType;
using std::set;
using std::string;

namespace chromeos_update_manager {

// Template instantiation for common types; used in BoxedValue::ToString().
// Keep in sync with boxed_value_unitttest.cc.

template <>
string BoxedValue::ValuePrinter<string>(const void* value) {
  const string* val = reinterpret_cast<const string*>(value);
  return *val;
}

template <>
string BoxedValue::ValuePrinter<int>(const void* value) {
  const int* val = reinterpret_cast<const int*>(value);
  return base::NumberToString(*val);
}

template <>
string BoxedValue::ValuePrinter<unsigned int>(const void* value) {
  const unsigned int* val = reinterpret_cast<const unsigned int*>(value);
  return base::NumberToString(*val);
}

template <>
string BoxedValue::ValuePrinter<int64_t>(const void* value) {
  const int64_t* val = reinterpret_cast<const int64_t*>(value);
  return base::NumberToString(*val);
}

template <>
string BoxedValue::ValuePrinter<uint64_t>(const void* value) {
  const uint64_t* val = reinterpret_cast<const uint64_t*>(value);
  return base::NumberToString(*val);
}

template <>
string BoxedValue::ValuePrinter<bool>(const void* value) {
  const bool* val = reinterpret_cast<const bool*>(value);
  return *val ? "true" : "false";
}

template <>
string BoxedValue::ValuePrinter<double>(const void* value) {
  const double* val = reinterpret_cast<const double*>(value);
  return base::NumberToString(*val);
}

template <>
string BoxedValue::ValuePrinter<base::Time>(const void* value) {
  const base::Time* val = reinterpret_cast<const base::Time*>(value);
  return chromeos_update_engine::utils::ToString(*val);
}

template <>
string BoxedValue::ValuePrinter<base::TimeDelta>(const void* value) {
  const base::TimeDelta* val = reinterpret_cast<const base::TimeDelta*>(value);
  return chromeos_update_engine::utils::FormatTimeDelta(*val);
}

template <>
string BoxedValue::ValuePrinter<ConnectionType>(const void* value) {
  const ConnectionType* val = reinterpret_cast<const ConnectionType*>(value);
  return StringForConnectionType(*val);
}

template <>
string BoxedValue::ValuePrinter<set<ConnectionType>>(const void* value) {
  string ret = "";
  const set<ConnectionType>* val =
      reinterpret_cast<const set<ConnectionType>*>(value);
  for (auto& it : *val) {
    ConnectionType type = it;
    if (ret.size() > 0) {
      ret += ",";
    }
    ret += StringForConnectionType(type);
  }
  return ret;
}

template <>
string BoxedValue::ValuePrinter<RollbackToTargetVersion>(const void* value) {
  const RollbackToTargetVersion* val =
      reinterpret_cast<const RollbackToTargetVersion*>(value);
  switch (*val) {
    case RollbackToTargetVersion::kUnspecified:
      return "Unspecified";
    case RollbackToTargetVersion::kDisabled:
      return "Disabled";
    case RollbackToTargetVersion::kRollbackAndPowerwash:
      return "Rollback and powerwash";
    case RollbackToTargetVersion::kRollbackAndRestoreIfPossible:
      return "Rollback and restore if possible";
    case RollbackToTargetVersion::kMaxValue:
      NOTREACHED_IN_MIGRATION();
      return "Max value";
  }
  NOTREACHED_IN_MIGRATION();
  return "Unknown";
}

template <>
string BoxedValue::ValuePrinter<Stage>(const void* value) {
  const Stage* val = reinterpret_cast<const Stage*>(value);
  switch (*val) {
    case Stage::kIdle:
      return "Idle";
    case Stage::kCheckingForUpdate:
      return "Checking For Update";
    case Stage::kUpdateAvailable:
      return "Update Available";
    case Stage::kDownloading:
      return "Downloading";
    case Stage::kVerifying:
      return "Verifying";
    case Stage::kFinalizing:
      return "Finalizing";
    case Stage::kUpdatedNeedReboot:
      return "Updated, Need Reboot";
    case Stage::kReportingErrorEvent:
      return "Reporting Error Event";
    case Stage::kAttemptingRollback:
      return "Attempting Rollback";
    case Stage::kCleanupPreviousUpdate:
      return "Cleanup Previous Update";
  }
  NOTREACHED_IN_MIGRATION();
  return "Unknown";
}

template <>
string BoxedValue::ValuePrinter<UpdateRequestStatus>(const void* value) {
  const UpdateRequestStatus* val =
      reinterpret_cast<const UpdateRequestStatus*>(value);
  switch (*val) {
    case UpdateRequestStatus::kNone:
      return "None";
    case UpdateRequestStatus::kInteractive:
      return "Interactive";
    case UpdateRequestStatus::kPeriodic:
      return "Periodic";
  }
  NOTREACHED_IN_MIGRATION();
  return "Unknown";
}

template <>
string BoxedValue::ValuePrinter<UpdateRestrictions>(const void* value) {
  const UpdateRestrictions* val =
      reinterpret_cast<const UpdateRestrictions*>(value);

  if (*val == UpdateRestrictions::kNone) {
    return "None";
  }
  string retval = "Flags:";
  if (*val & kRestrictDownloading) {
    retval += " RestrictDownloading";
  }
  return retval;
}

template <>
string BoxedValue::ValuePrinter<WeeklyTimeInterval>(const void* value) {
  const WeeklyTimeInterval* val =
      reinterpret_cast<const WeeklyTimeInterval*>(value);
  return val->ToString();
}

template <>
string BoxedValue::ValuePrinter<WeeklyTimeIntervalVector>(const void* value) {
  const WeeklyTimeIntervalVector* val =
      reinterpret_cast<const WeeklyTimeIntervalVector*>(value);

  string retval = "Disallowed intervals:\n";
  for (const auto& interval : *val) {
    retval += interval.ToString() + "\n";
  }
  return retval;
}

template <>
string BoxedValue::ValuePrinter<ChannelDowngradeBehavior>(const void* value) {
  const ChannelDowngradeBehavior* val =
      reinterpret_cast<const ChannelDowngradeBehavior*>(value);
  switch (*val) {
    case ChannelDowngradeBehavior::kUnspecified:
      return "Unspecified";
    case ChannelDowngradeBehavior::kWaitForVersionToCatchUp:
      return "Wait for the target channel to catch up";
    case ChannelDowngradeBehavior::kRollback:
      return "Roll back and powerwash on channel downgrade";
    case ChannelDowngradeBehavior::kAllowUserToConfigure:
      return "User decides on channel downgrade behavior";
  }
  NOTREACHED_IN_MIGRATION();
  return "Unknown";
}

template <>
string BoxedValue::ValuePrinter<base::Version>(const void* value) {
  const base::Version* val = reinterpret_cast<const base::Version*>(value);
  if (val->IsValid()) {
    return val->GetString();
  }
  return "Unknown";
}

}  // namespace chromeos_update_manager
