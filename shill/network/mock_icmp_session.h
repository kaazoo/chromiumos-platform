// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_MOCK_ICMP_SESSION_H_
#define SHILL_NETWORK_MOCK_ICMP_SESSION_H_

#include "shill/network/icmp_session.h"

#include <gmock/gmock.h>

namespace shill {

class MockIcmpSession : public IcmpSession {
 public:
  explicit MockIcmpSession(EventDispatcher* dispatcher);
  MockIcmpSession(const MockIcmpSession&) = delete;
  MockIcmpSession& operator=(const MockIcmpSession&) = delete;

  ~MockIcmpSession() override;

  MOCK_METHOD(bool,
              Start,
              (const net_base::IPAddress&,
               int,
               std::string_view,
               IcmpSession::IcmpSessionResultCallback),
              (override));
  MOCK_METHOD(void, Stop, (), (override));
};

}  // namespace shill

#endif  // SHILL_NETWORK_MOCK_ICMP_SESSION_H_
