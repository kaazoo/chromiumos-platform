// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>

#include "installer/inst_util.h"

int main(int argc, char** argv) {
  DEFINE_string(device, "",
                "Path of the device to run the partition migration on");
  DEFINE_int32(reclaimed_partition_num, 0, "Which partition to reclaim");
  DEFINE_bool(revert, false, "Revert the migration");

  brillo::FlagHelper::Init(argc, argv, "Chromium OS Partition Migrator");

  const base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->GetArgs().size() > 0) {
    LOG(ERROR) << "Usage: cros_partition_migrator --device=<block device> "
               << "--reclaimed_partition_num=<num> [--revert]";
  }

  bool success =
      installer::MigratePartition(base::FilePath(FLAGS_device),
                                  FLAGS_reclaimed_partition_num, FLAGS_revert);

  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
