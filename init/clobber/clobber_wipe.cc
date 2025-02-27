// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber/clobber_wipe.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <linux/fs.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/bits.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <brillo/files/file_util.h>
#include <brillo/process/process.h>
#include <brillo/blkdev_utils/get_backing_block_device.h>
#include <brillo/blkdev_utils/storage_device.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <chromeos/secure_erase_file/secure_erase_file.h>

#include "init/clobber/clobber_state_log.h"
#include "init/utils.h"

namespace {
bool GetBlockCount(const base::FilePath& device_path,
                   int64_t block_size,
                   int64_t* block_count_out) {
  if (!block_count_out) {
    return false;
  }

  brillo::ProcessImpl dumpe2fs;
  dumpe2fs.AddArg("/sbin/dumpe2fs");
  dumpe2fs.AddArg("-h");
  dumpe2fs.AddArg(device_path.value());

  dumpe2fs.RedirectOutputToMemory(true);
  if (dumpe2fs.Run() == 0) {
    std::string output = dumpe2fs.GetOutputString(STDOUT_FILENO);
    size_t label = output.find("Block count");
    size_t value_start = output.find_first_of("0123456789", label);
    size_t value_end = output.find_first_not_of("0123456789", value_start);

    if (value_start != std::string::npos && value_end != std::string::npos) {
      int64_t block_count;
      if (base::StringToInt64(
              output.substr(value_start, value_end - value_start),
              &block_count)) {
        *block_count_out = block_count;
        return true;
      }
    }
  }

  // Fallback if using dumpe2fs failed. This interface always returns a count
  // of sectors, not blocks, so we must convert to a block count.
  // Per "include/linux/types.h", Linux always considers sectors to be
  // 512 bytes long.
  base::FilePath fp("/sys/class/block");
  fp = fp.Append(device_path.BaseName());
  fp = fp.Append("size");
  std::string sector_count_str;
  if (base::ReadFileToString(fp, &sector_count_str)) {
    base::TrimWhitespaceASCII(sector_count_str, base::TRIM_ALL,
                              &sector_count_str);
    int64_t sector_count;
    if (base::StringToInt64(sector_count_str, &sector_count)) {
      *block_count_out = sector_count * 512 / block_size;
      return true;
    }
  }
  return false;
}
}  // namespace

ClobberWipe::ClobberWipe(ClobberUi* ui) : ui_(ui), dev_("/dev"), sys_("/sys") {}

bool ClobberWipe::WipeDevice(const base::FilePath& device_path, bool discard) {
  const int write_block_size = 4 * 1024 * 1024;
  int64_t to_write = 0;

  struct stat st;
  if (stat(device_path.value().c_str(), &st) == -1) {
    PLOG(ERROR) << "Unable to stat " << device_path.value();
    return false;
  }

  if (fast_wipe_) {
    to_write = write_block_size;
  } else {
    // Wipe the filesystem size if we can determine it. Full partition wipe
    // takes a long time on 16G SSD or rotating media.
    int64_t block_size = st.st_blksize;
    int64_t block_count;
    if (!GetBlockCount(device_path, block_size, &block_count)) {
      LOG(ERROR) << "Unable to get block count for " << device_path.value();
      return false;
    }
    to_write = block_count * block_size;
    LOG(INFO) << "Filesystem block size: " << block_size;
    LOG(INFO) << "Filesystem block count: " << block_count;
  }

  LOG(INFO) << "Wiping block device " << device_path.value()
            << (fast_wipe_ ? " (fast) " : "");
  LOG(INFO) << "Number of bytes to write: " << to_write;

  base::File device(open(device_path.value().c_str(), O_WRONLY | O_SYNC));
  if (!device.IsValid()) {
    PLOG(ERROR) << "Unable to open " << device_path.value();
    return false;
  }

  // Don't display progress in fast mode since it runs so quickly.
  bool display_progress = !fast_wipe_;
  base::ScopedClosureRunner stop_wipe_ui;
  if (display_progress) {
    if (ui_->StartWipeUi(to_write)) {
      stop_wipe_ui.ReplaceClosure(
          base::BindOnce([](ClobberUi* ui) { ui->StopWipeUi(); }, ui_));
    } else {
      display_progress = false;
    }
  }

  uint64_t total_written = 0;

  // We call wiping in chunks 5% (1/20th) of the disk size so that we can
  // update progress as we go. Round up the chunk size to a multiple of 128MiB,
  // since the wiping ioctl requires that its arguments are aligned to at least
  // 512 bytes.
  const uint64_t zero_block_size = base::bits::AlignUp(
      static_cast<uint64_t>(to_write / 20), uint64_t{128 * 1024 * 1024});
  const uint64_t zero_block_size_1mib = base::bits::AlignUp(
      static_cast<uint64_t>(to_write / 20), uint64_t{1024 * 1024});

  base::FilePath base_dev =
      brillo::GetBackingPhysicalDeviceForBlock(st.st_rdev);
  std::unique_ptr<brillo::StorageDevice> storage_device =
      brillo::GetStorageDevice(base_dev);
  while (total_written < to_write) {
    uint64_t write_size = std::min(zero_block_size, to_write - total_written);
    // For `discard` case, chunk smaller for first 128MiB wipes.
    if (discard && total_written < zero_block_size) {
      write_size = std::min(zero_block_size_1mib, to_write - total_written);
    }
    if (!storage_device->WipeBlkDev(device_path, total_written, write_size,
                                    false, discard)) {
      break;
    }
    total_written += write_size;
    if (display_progress) {
      ui_->UpdateWipeProgress(total_written);
    }
  }

  if (total_written == to_write) {
    LOG(INFO) << "Successfully zeroed " << to_write << " bytes on "
              << device_path.value();
    return true;
  }
  LOG(INFO) << "Reverting to manual wipe for bytes " << total_written
            << " through " << to_write;

  const std::vector<char> buffer(write_block_size, '\0');
  while (total_written < to_write) {
    int write_size = std::min(static_cast<uint64_t>(write_block_size),
                              to_write - total_written);
    int64_t bytes_written = device.WriteAtCurrentPos(buffer.data(), write_size);
    if (bytes_written < 0) {
      PLOG(ERROR) << "Failed to write to " << device_path.value();
      LOG(ERROR) << "Wrote " << total_written << " bytes before failing";
      return false;
    }
    if (discard && !storage_device->DiscardBlockDevice(
                       device_path, total_written, write_size)) {
      PLOG(ERROR) << "Failed to discard blocks of " << device_path.value()
                  << " at offset=" << total_written << " size=" << write_size;
      return false;
    }
    total_written += bytes_written;
    if (display_progress) {
      ui_->UpdateWipeProgress(total_written);
    }
  }
  LOG(INFO) << "Successfully wrote " << total_written << " bytes to "
            << device_path.value();

  return true;
}

// Wrapper around secure_erase_file::SecureErase(const base::FilePath&).
bool ClobberWipe::SecureErase(const base::FilePath& path) {
  return secure_erase_file::SecureErase(path);
}

// Wrapper around secure_erase_file::DropCaches(). Must be called after
// a call to SecureErase. Files are only securely deleted if DropCaches
// returns true.
bool ClobberWipe::DropCaches() {
  return secure_erase_file::DropCaches();
}

int ClobberWipe::Stat(const base::FilePath& path, struct stat* st) {
  return stat(path.value().c_str(), st);
}

bool ClobberWipe::IsRotational(const base::FilePath& device_path) {
  if (!dev_.IsParent(device_path)) {
    LOG(ERROR) << "Non-device given as argument to IsRotational: "
               << device_path.value();
    return false;
  }

  // Since there doesn't seem to be a good way to get from a partition name
  // to the base device name beyond simple heuristics, just find the device
  // with the same major number but with minor 0.
  // TODO(b:172226877) : this is broken:
  // Technically, the minor could be a multiple of 16 for SCSI devices
  // Does not work when LVM is used.
  struct stat st;
  if (Stat(device_path, &st) != 0) {
    return false;
  }
  unsigned int major_device_number = major(st.st_rdev);

  base::FileEnumerator enumerator(dev_, /*recursive=*/true,
                                  base::FileEnumerator::FileType::FILES);
  for (base::FilePath base_device_path = enumerator.Next();
       !base_device_path.empty(); base_device_path = enumerator.Next()) {
    if (Stat(base_device_path, &st) == 0 && S_ISBLK(st.st_mode) &&
        major(st.st_rdev) == major_device_number && minor(st.st_rdev) == 0) {
      // |base_device_path| must be the base device for |device_path|.
      base::FilePath rotational_file = sys_.Append("block")
                                           .Append(base_device_path.BaseName())
                                           .Append("queue/rotational");

      int value;
      if (utils::ReadFileToInt(rotational_file, &value)) {
        return value == 1;
      }
    }
  }
  return false;
}
