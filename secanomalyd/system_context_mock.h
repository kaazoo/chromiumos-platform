// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECANOMALYD_SYSTEM_CONTEXT_MOCK_H_
#define SECANOMALYD_SYSTEM_CONTEXT_MOCK_H_

#include "secanomalyd/system_context.h"

#include <gmock/gmock.h>

class SystemContextMock : public SystemContext {
 public:
  explicit SystemContextMock(bool logged_in) { set_logged_in(logged_in); }

  MOCK_METHOD(void, Refresh, (), (override));
};

#endif  // SECANOMALYD_SYSTEM_CONTEXT_MOCK_H_
