// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "patchpanel/qos_service.h"

#include "patchpanel/datapath.h"

namespace patchpanel {

QoSService::QoSService(Datapath* datapath) : datapath_(datapath) {}

QoSService::~QoSService() = default;

}  // namespace patchpanel
