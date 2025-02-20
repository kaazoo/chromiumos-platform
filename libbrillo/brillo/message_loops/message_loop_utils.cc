// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/message_loops/message_loop_utils.h>

#include <base/functional/bind.h>
#include <base/location.h>

namespace brillo {

void MessageLoopRunUntil(MessageLoop* loop,
                         base::TimeDelta timeout,
                         base::RepeatingCallback<bool()> terminate) {
  bool timeout_called = false;
  MessageLoop::TaskId task_id = loop->PostDelayedTask(
      FROM_HERE,
      base::BindOnce([](bool* timeout_called) { *timeout_called = true; },
                     &timeout_called),
      timeout);
  while (!timeout_called && (terminate.is_null() || !terminate.Run())) {
    loop->RunOnce(true);
  }

  if (!timeout_called) {
    loop->CancelTask(task_id);
  }
}

int MessageLoopRunMaxIterations(MessageLoop* loop, int iterations) {
  int result;
  for (result = 0; result < iterations && loop->RunOnce(false); result++) {
  }
  return result;
}

}  // namespace brillo
