// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HAMMERD_MOCK_DBUS_WRAPPER_H_
#define HAMMERD_MOCK_DBUS_WRAPPER_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "hammerd/dbus_wrapper.h"

namespace hammerd {

class MockDBusWrapper : public DBusWrapperInterface {
 public:
  MockDBusWrapper() = default;

  MOCK_METHOD1(SendSignal, void(const std::string& signal_name));
  MOCK_METHOD2(SendSignalWithArgHelper, void(const std::string& signal_name,
                                             const std::vector<uint8_t> arg));

  void SendSignalWithArg(const std::string& signal_name,
                         const uint8_t* values, size_t length) {
    // We only care about the argument value, instead of the address.
    std::vector<uint8_t> arg(values, values + length);
    SendSignalWithArgHelper(signal_name, arg);
  }
};

}  // namespace hammerd
#endif  // HAMMERD_MOCK_DBUS_WRAPPER_H_
