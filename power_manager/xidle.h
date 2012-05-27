// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_XIDLE_H_
#define POWER_MANAGER_XIDLE_H_

#include <list>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/memory/scoped_ptr.h"
#include "power_manager/idle_interface.h"
#include "power_manager/signal_callback.h"
#include "power_manager/xevent_observer.h"
#include "power_manager/xsync_interface.h"

namespace power_manager {

// Receive notifications from the X Server when the user is marked as idle,
// or as no longer idle.
//
// See examples/xidle_example.cc for usage example.
class XIdle : public IdleInterface, public XEventObserverInterface {
 public:
  XIdle();
  // This constructor takes ownership of |xsync| by saving it in a scoped ptr.
  // The caller should not attempt to delete |xsync|.
  explicit XIdle(XSyncInterface *xsync);
  virtual ~XIdle();

  // Initialize the object with the given |observer|.
  // On success, return true; otherwise return false.
  // The flag |check_xsync_version| is used for testing, when the target system
  // XSync may not be the same version as the host system's XSync.  Set it to
  // false to disable the version check so that the test can pass.
  bool Init(IdleObserver* observer, bool check_xsync_version);

  // This version of Init() defaults to check_xsync_version=true.  This is the
  // intended behavior when running on the target system (i.e. not in a unit
  // test).
  virtual bool Init(IdleObserver* observer) OVERRIDE;

  // Add a timeout value. Idle events will be fired every time
  // the user either becomes newly idle (due to exceeding an idle
  // timeout) or is no longer idle.
  //
  // On success, return true; otherwise return false.
  virtual bool AddIdleTimeout(int64 idle_timeout_ms) OVERRIDE;

  // Set *idle_time_ms to how long the user has been idle, in milliseconds.
  // On success, return true; otherwise return false.
  virtual bool GetIdleTime(int64* idle_time_ms) OVERRIDE;

  // Clear all timeouts.
  // On success, return true; otherwise return false.
  virtual bool ClearTimeouts() OVERRIDE;

 private:
  // Create an XSyncAlarm. Returns the new alarm.
  //
  // If test_type is XSyncPositiveTransition, the alarm triggers when the
  // idle timeout is exceeded. On the other hand, if test_type is
  // XSyncNegativeTransition, the alarm triggers when the user is no longer
  // idle.
  XSyncAlarm CreateIdleAlarm(int64 idle_timeout_ms, XSyncTestType test_type);

  // Callback to handle XSync events.
  // Inherited from XEventObserver.
  virtual XEventHandlerStatus HandleXEvent(XEvent* event) OVERRIDE;

  // Wrapper object for making XSync calls.  Allows XSync API to be mocked out
  // during testing.
  scoped_ptr<XSyncInterface> xsync_;

  XSyncCounter idle_counter_;
  int64 min_timeout_;
  int event_base_, error_base_;

  // Non-owned pointer to object listening for changes to idle state.
  IdleObserver* observer_;

  // A list of the alarms. If non-empty, the negative transition alarm for
  // min_timeout_ will be the first alarm in the list.
  std::list<XSyncAlarm> alarms_;

  DISALLOW_COPY_AND_ASSIGN(XIdle);
};

}  // namespace power_manager

#endif  // POWER_MANAGER_XIDLE_H_
