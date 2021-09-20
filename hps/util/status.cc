// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Read status registers.
 */

#include <iomanip>
#include <iostream>
#include <memory>

#include <base/command_line.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>

#include "hps/dev.h"
#include "hps/hps.h"
#include "hps/hps_reg.h"
#include "hps/util/command.h"
#include "hps/utils.h"

namespace {

// No arguments, registers 0 - 4 are dumped
// N - dump register N
// N - M Dump registers between N and M inclusive
int Status(std::unique_ptr<hps::HPS> hps,
           const base::CommandLine::StringVector& args) {
  int start, end;
  switch (args.size()) {
    case 1:
      start = 0;
      end = 4;
      break;

    case 2:
      if (!base::StringToInt(args[1], &start)) {
        std::cerr << args[1] << ": illegal register" << std::endl;
        return 1;
      }
      end = start;
      break;

    case 3:
      if (!base::StringToInt(args[1], &start)) {
        std::cerr << args[1] << ": illegal register" << std::endl;
        return 1;
      }
      if (!base::StringToInt(args[2], &end)) {
        std::cerr << args[2] << ": illegal register" << std::endl;
        return 1;
      }
      break;

    default:
      std::cerr << "status: arg error" << std::endl;
      return 1;
  }
  if (start < 0 || start > static_cast<int>(hps::HpsReg::kMax)) {
    std::cerr << "status: illegal start value" << std::endl;
    return 1;
  }
  if (end < 0 || end > static_cast<int>(hps::HpsReg::kMax)) {
    std::cerr << "status: illegal end value" << std::endl;
    return 1;
  }
  if (end < start) {
    std::cerr << "status: end < start, nothing to do" << std::endl;
    return 1;
  }

  for (auto i = start; i <= end; i++) {
    int result = hps->Device()->ReadReg(hps::HpsReg(i));
    if (result < 0) {
      std::cout << base::StringPrintf("Register %3d: error (%s)\n", i,
                                      hps::HpsRegToString(hps::HpsReg(i)));
    } else {
      std::cout << base::StringPrintf("Register %3d: 0x%.4x (%s)\n", i,
                                      static_cast<uint16_t>(result),
                                      hps::HpsRegToString(hps::HpsReg(i)));
    }
  }
  return 0;
}

Command status("status",
               "status [ start [ end ] ] - "
               "Dump status registers (default 0 5).",
               Status);

}  // namespace
