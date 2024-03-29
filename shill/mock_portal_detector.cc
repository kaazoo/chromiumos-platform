// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_portal_detector.h"

namespace shill {

MockPortalDetector::MockPortalDetector()
    : PortalDetector(nullptr, nullptr, "wlan1", {}, "tag") {}

MockPortalDetector::~MockPortalDetector() = default;

}  // namespace shill
