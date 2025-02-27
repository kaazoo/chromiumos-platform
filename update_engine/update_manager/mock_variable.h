// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_MANAGER_MOCK_VARIABLE_H_
#define UPDATE_ENGINE_UPDATE_MANAGER_MOCK_VARIABLE_H_

#include <string>

#include <gmock/gmock.h>

#include "update_engine/update_manager/variable.h"

namespace chromeos_update_manager {

// This is a generic mock of the Variable class.
template <typename T>
class MockVariable : public Variable<T> {
 public:
  MockVariable(const MockVariable&) = delete;
  MockVariable& operator=(const MockVariable&) = delete;
  using Variable<T>::Variable;

  MOCK_METHOD2_T(GetValue, const T*(base::TimeDelta, std::string*));
};

}  // namespace chromeos_update_manager

#endif  // UPDATE_ENGINE_UPDATE_MANAGER_MOCK_VARIABLE_H_
