// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Access via MCP2221A device.
 */
#ifndef HPS_HAL_MCP_H_
#define HPS_HAL_MCP_H_

#include <memory>

#include <libusb-1.0/libusb.h>

#include "hps/dev.h"

namespace hps {

inline static constexpr int kMcpTransferSize = 64;  // Transfer buffer size

class Mcp : public DevInterface {
 public:
  virtual ~Mcp();
  void Close();
  bool Read(uint8_t cmd, uint8_t* data, size_t len) override;
  bool Write(uint8_t cmd, const uint8_t* data, size_t len) override;
  static std::unique_ptr<DevInterface> Create(uint8_t address);

 private:
  explicit Mcp(uint8_t addr) : address_(addr << 1), context_(0), handle_(0) {}
  bool Init();
  bool PrepareBus();
  bool Cmd();
  void Clear();

  uint8_t address_;
  libusb_context* context_;
  libusb_device_handle* handle_;
  uint8_t in_[kMcpTransferSize];
  uint8_t out_[kMcpTransferSize];
};

}  // namespace hps

#endif  // HPS_HAL_MCP_H_
