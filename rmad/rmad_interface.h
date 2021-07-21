// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_RMAD_INTERFACE_H_
#define RMAD_RMAD_INTERFACE_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <rmad/proto_bindings/rmad.pb.h>

namespace rmad {

class RmadInterface {
 public:
  RmadInterface() = default;
  virtual ~RmadInterface() = default;

  // Register a signal sender for specific states. Virtual functions cannot be
  // declared as template so we need to declare them one by one.
  virtual void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<base::RepeatingCallback<bool(bool)>> callback) = 0;

  using CalibrationSignalCallback = base::RepeatingCallback<bool(
      CheckCalibrationState::CalibrationStatus, double)>;
  virtual void RegisterSignalSender(
      RmadState::StateCase state_case,
      std::unique_ptr<CalibrationSignalCallback> callback) = 0;

  // Get the current state_case.
  virtual RmadState::StateCase GetCurrentStateCase() = 0;

  // Callback used by all state functions to return the current state to the
  // dbus service.
  using GetStateCallback = base::Callback<void(const GetStateReply&)>;

  // Get the initialized current RmadState proto.
  virtual void GetCurrentState(const GetStateCallback& callback) = 0;
  // Update the state using the RmadState proto in the request and return the
  // resulting state after all work is done.
  virtual void TransitionNextState(const TransitionNextStateRequest& request,
                                   const GetStateCallback& callback) = 0;
  // Go back to the previous state if possible and return the RmadState proto.
  virtual void TransitionPreviousState(const GetStateCallback& callback) = 0;

  using AbortRmaCallback = base::Callback<void(const AbortRmaReply&)>;
  // Cancel the RMA process if possible and reboot.
  virtual void AbortRma(const AbortRmaCallback& callback) = 0;

  // Returns whether it's allowed to abort RMA now.
  virtual bool AllowAbort() const = 0;

  using GetLogPathCallback = base::Callback<void(const std::string&)>;
  // Get the path to the RMA log file.
  virtual void GetLogPath(const GetLogPathCallback& callback) = 0;
};

}  // namespace rmad

#endif  // RMAD_RMAD_INTERFACE_H_
