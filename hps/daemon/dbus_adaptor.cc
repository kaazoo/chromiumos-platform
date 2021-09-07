// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/dbus_adaptor.h"

#include <utility>

#include <base/location.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/service_constants.h>

namespace hps {

constexpr char kErrorPath[] = "org.chromium.Hps.GetFeatureResultError";

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus,
                         std::unique_ptr<HPS> hps,
                         uint32_t poll_time_ms)
    : org::chromium::HpsAdaptor(this),
      dbus_object_(nullptr, bus, dbus::ObjectPath(::hps::kHpsServicePath)),
      hps_(std::move(hps)),
      poll_time_ms_(poll_time_ms) {}

void DBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void DBusAdaptor::PollTask() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (uint8_t feature = 0; feature < kFeatures; ++feature) {
    if (enabled_features_.test(feature)) {
      int result = this->hps_->Result(feature);
      if (result >= 0) {
        // TODO(evanbenn): Do something with the result.
      }
    }
  }
}

bool DBusAdaptor::EnableFeature(brillo::ErrorPtr* error,
                                const hps::FeatureConfig& config,
                                uint8_t feature) {
  // TODO(evanbenn) Add the filter factory here

  if (!this->hps_->Enable(feature)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Unable to enable feature");

    return false;
  } else {
    enabled_features_.set(feature);

    if (enabled_features_.any() && !poll_timer_.IsRunning()) {
      poll_timer_.Start(
          FROM_HERE, base::TimeDelta::FromMilliseconds(poll_time_ms_),
          base::BindRepeating(&DBusAdaptor::PollTask, base::Unretained(this)));
    }
    return true;
  }
}

bool DBusAdaptor::DisableFeature(brillo::ErrorPtr* error, uint8_t feature) {
  if (!this->hps_->Disable(feature)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Unable to disable feature");

    return false;
  } else {
    enabled_features_.reset(feature);
    if (enabled_features_.none()) {
      poll_timer_.Stop();
    }
    return true;
  }
}

bool DBusAdaptor::GetFeatureResult(brillo::ErrorPtr* error,
                                   bool* result,
                                   uint8_t feature) {
  int res = this->hps_->Result(feature);
  if (res < 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kErrorPath, "hpsd: Feature result not available");

    return false;
  } else {
    *result = res;
    return true;
  }
}

bool DBusAdaptor::EnableHpsSense(brillo::ErrorPtr* error,
                                 const hps::FeatureConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return EnableFeature(error, config, 0);
}

bool DBusAdaptor::DisableHpsSense(brillo::ErrorPtr* error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return DisableFeature(error, 0);
}

bool DBusAdaptor::GetResultHpsSense(brillo::ErrorPtr* error, bool* result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetFeatureResult(error, result, 0);
}

bool DBusAdaptor::EnableHpsNotify(brillo::ErrorPtr* error,
                                  const hps::FeatureConfig& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return EnableFeature(error, config, 1);
}

bool DBusAdaptor::DisableHpsNotify(brillo::ErrorPtr* error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return DisableFeature(error, 1);
}

bool DBusAdaptor::GetResultHpsNotify(brillo::ErrorPtr* error, bool* result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetFeatureResult(error, result, 1);
}

}  // namespace hps
