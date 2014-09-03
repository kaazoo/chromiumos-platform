// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_BACKLIGHT_CONTROLLER_OBSERVER_STUB_H_
#define POWER_MANAGER_POWERD_POLICY_BACKLIGHT_CONTROLLER_OBSERVER_STUB_H_

#include <utility>
#include <vector>

#include <base/compiler_specific.h>
#include <base/macros.h>

#include "power_manager/powerd/policy/backlight_controller.h"
#include "power_manager/powerd/policy/backlight_controller_observer.h"

namespace power_manager {
namespace policy {

// Simple test class that records backlight brightness changes.
class BacklightControllerObserverStub : public BacklightControllerObserver {
 public:
  struct ChangeTuple {
    double percent;
    BacklightController::BrightnessChangeCause cause;
    BacklightController* source;
  };

  BacklightControllerObserverStub();
  virtual ~BacklightControllerObserverStub();

  const std::vector<ChangeTuple>& changes() const { return changes_; }

  // Clears |changes_|.
  void Clear();

  // BacklightControllerObserver implementation:
  void OnBrightnessChanged(
      double brightness_percent,
      BacklightController::BrightnessChangeCause cause,
      BacklightController* source) override;

 private:
  // Received changes, in oldest-to-newest order.
  std::vector<ChangeTuple> changes_;

  DISALLOW_COPY_AND_ASSIGN(BacklightControllerObserverStub);
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_BACKLIGHT_CONTROLLER_OBSERVER_STUB_H_
