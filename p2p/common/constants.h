// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef P2P_COMMON_CONSTANTS_H__
#define P2P_COMMON_CONSTANTS_H__

#include <base/basictypes.h>

namespace p2p {

namespace constants {

// The maximum number of simulatenous downloads in the LAN.
constexpr int kMaxSimultaneousDownloads = 3;

// The number of seconds to wait when waiting for the number
// of p2p downloads in the LAN to drop below
// kMaxSimultaneousDownloads.
constexpr int kMaxSimultaneousDownloadsPollTimeSeconds = 30;

// The maximum rate per download, in bytes per second.
constexpr int64 kMaxSpeedPerDownload = 1 * 1000 * 1000;

// The name of p2p server binary.
constexpr char kServerBinaryName[] = "p2p-server";

// The name of p2p HTTP server binary.
constexpr char kHttpServerBinaryName[] = "p2p-http-server";

// The default TCP port for the HTTP server ("AU").
constexpr uint16 kHttpServerDefaultPort = 16725;

}  // namespace constants

}  // namespace p2p

#endif  // P2P_COMMON_CONSTANTS_H__
