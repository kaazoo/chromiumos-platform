// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_generator/squashfs_filesystem.h"

#include <fcntl.h>

#include <algorithm>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <brillo/streams/file_stream.h>

#include "update_engine/common/subprocess.h"
#include "update_engine/common/utils.h"
#include "update_engine/payload_generator/deflate_utils.h"
#include "update_engine/payload_generator/delta_diff_generator.h"
#include "update_engine/payload_generator/extent_ranges.h"
#include "update_engine/payload_generator/extent_utils.h"
#include "update_engine/update_metadata.pb.h"

using base::FilePath;
using base::ScopedTempDir;
using std::string;
using std::unique_ptr;
using std::vector;

namespace chromeos_update_engine {

namespace {

// The size of the squashfs super block.
constexpr size_t kSquashfsSuperBlockSize = 96;
constexpr uint64_t kSquashfsCompressedBit = 1 << 24;
constexpr uint32_t kSquashfsZlibCompression = 1;

constexpr char kUpdateEngineConf[] = "etc/update_engine.conf";

bool ReadSquashfsHeader(const brillo::Blob blob,
                        SquashfsFilesystem::SquashfsHeader* header) {
  if (blob.size() < kSquashfsSuperBlockSize) {
    return false;
  }

  memcpy(&header->magic, blob.data(), 4);
  memcpy(&header->block_size, blob.data() + 12, 4);
  memcpy(&header->compression_type, blob.data() + 20, 2);
  memcpy(&header->major_version, blob.data() + 28, 2);
  return true;
}

bool CheckHeader(const SquashfsFilesystem::SquashfsHeader& header) {
  return header.magic == 0x73717368 && header.major_version == 4;
}

bool GetFileMapContent(const string& sqfs_path, string* map) {
  ScopedTempFile map_file("squashfs_file_map.XXXXXX");
  // Run unsquashfs to get the system file map.
  // unsquashfs -m <map-file> <squashfs-file>
  vector<string> cmd = {"unsquashfs", "-m", map_file.path(), sqfs_path};
  string stdout, stderr;
  int exit_code;
  if (!Subprocess::SynchronousExec(cmd, &exit_code, &stdout, &stderr) ||
      exit_code != 0) {
    LOG(ERROR) << "Failed to run `unsquashfs -m` with stdout content: "
               << stdout << " and stderr content: " << stderr;
    return false;
  }
  TEST_AND_RETURN_FALSE(utils::ReadFile(map_file.path(), map));
  return true;
}

bool GetUpdateEngineConfig(const std::string& sqfs_path, string* config) {
  ScopedTempDir unsquash_dir;
  if (!unsquash_dir.CreateUniqueTempDir()) {
    PLOG(ERROR) << "Failed to create a temporary directory.";
    return false;
  }

  // Run unsquashfs to extract update_engine.conf
  // -f: To force overriding if the target directory exists.
  // -d: The directory to unsquash the files.
  vector<string> cmd = {"unsquashfs", "-f",
                        "-d",         unsquash_dir.GetPath().value(),
                        sqfs_path,    kUpdateEngineConf};
  string stdout, stderr;
  int exit_code;
  if (!Subprocess::SynchronousExec(cmd, &exit_code, &stdout, &stderr) ||
      exit_code != 0) {
    PLOG(ERROR) << "Failed to unsquashfs etc/update_engine.conf with stdout: "
                << stdout << " and stderr: " << stderr;
    return false;
  }

  auto config_path = unsquash_dir.GetPath().Append(kUpdateEngineConf);
  string config_content;
  if (!utils::ReadFile(config_path.value(), &config_content)) {
    PLOG(ERROR) << "Failed to read " << config_path.value();
    return false;
  }

  if (config_content.empty()) {
    LOG(ERROR) << "update_engine config file was empty!!";
    return false;
  }

  *config = std::move(config_content);
  return true;
}

}  // namespace

bool SquashfsFilesystem::Init(const string& map,
                              const string& sqfs_path,
                              size_t size,
                              const SquashfsHeader& header,
                              bool extract_deflates) {
  size_ = size;

  bool is_zlib = header.compression_type == kSquashfsZlibCompression;
  if (!is_zlib) {
    LOG(WARNING) << "Filesystem is not Gzipped. Not filling deflates!";
  }
  vector<puffin::ByteExtent> zlib_blks;

  // Reading files map. For the format of the file map look at the comments for
  // |CreateFromFileMap()|.
  auto lines = base::SplitStringPiece(map, "\n",
                                      base::WhitespaceHandling::KEEP_WHITESPACE,
                                      base::SplitResult::SPLIT_WANT_NONEMPTY);
  for (const auto& line : lines) {
    auto splits = base::SplitStringPiece(
        line, " \t", base::WhitespaceHandling::TRIM_WHITESPACE,
        base::SplitResult::SPLIT_WANT_NONEMPTY);
    // Only filename is invalid.
    TEST_AND_RETURN_FALSE(splits.size() > 1);
    uint64_t start;
    TEST_AND_RETURN_FALSE(base::StringToUint64(splits[1], &start));
    uint64_t cur_offset = start;
    bool is_compressed = false;
    for (size_t i = 2; i < splits.size(); ++i) {
      uint64_t blk_size;
      TEST_AND_RETURN_FALSE(base::StringToUint64(splits[i], &blk_size));
      // TODO(ahassani): For puffin push it into a proper list if uncompressed.
      auto new_blk_size = blk_size & ~kSquashfsCompressedBit;
      TEST_AND_RETURN_FALSE(new_blk_size <= header.block_size);
      if (new_blk_size > 0 && !(blk_size & kSquashfsCompressedBit)) {
        // It is a compressed block.
        if (is_zlib && extract_deflates) {
          zlib_blks.emplace_back(cur_offset, new_blk_size);
        }
        is_compressed = true;
      }
      cur_offset += new_blk_size;
    }

    // If size is zero do not add the file.
    if (cur_offset - start > 0) {
      File file;
      file.name = std::string(splits[0]);
      file.extents = {ExtentForBytes(kBlockSize, start, cur_offset - start)};
      file.is_compressed = is_compressed;
      files_.emplace_back(file);
    }
  }

  // Sort all files by their offset in the squashfs.
  std::sort(files_.begin(), files_.end(), [](const File& a, const File& b) {
    return a.extents[0].start_block() < b.extents[0].start_block();
  });
  // If there is any overlap between two consecutive extents, remove them. Here
  // we are assuming all files have exactly one extent. If this assumption
  // changes then this implementation needs to change too.
  for (auto first = files_.begin(),
            second = first + (first == files_.end() ? 0 : 1);
       first != files_.end() && second != files_.end(); second = first + 1) {
    auto first_begin = first->extents[0].start_block();
    auto first_end = first_begin + first->extents[0].num_blocks();
    auto second_begin = second->extents[0].start_block();
    auto second_end = second_begin + second->extents[0].num_blocks();
    // Remove the first file if the size is zero.
    if (first_end == first_begin) {
      first = files_.erase(first);
    } else if (first_end > second_begin) {  // We found a collision.
      if (second_end <= first_end) {
        // Second file is inside the first file, remove the second file.
        second = files_.erase(second);
      } else if (first_begin == second_begin) {
        // First file is inside the second file, remove the first file.
        first = files_.erase(first);
      } else {
        // Remove overlapping extents from the first file.
        first->extents[0].set_num_blocks(second_begin - first_begin);
        ++first;
      }
    } else {
      ++first;
    }
  }

  // Find all the metadata including superblock and add them to the list of
  // files.
  ExtentRanges file_extents;
  for (const auto& file : files_) {
    file_extents.AddExtents(file.extents);
  }
  vector<Extent> full = {ExtentForBytes(kBlockSize, 0, size_)};
  auto metadata_extents = FilterExtentRanges(full, file_extents);
  // For now there should be at most two extents. One for superblock and one for
  // metadata at the end. Just create appropriate files with <metadata-i> name.
  // We can add all these extents as one metadata too, but that violates the
  // contiguous write optimization.
  for (size_t i = 0; i < metadata_extents.size(); i++) {
    File file;
    file.name = "<metadata-" + std::to_string(i) + ">";
    file.extents = {metadata_extents[i]};
    files_.emplace_back(file);
  }

  // Do one last sort before returning.
  std::sort(files_.begin(), files_.end(), [](const File& a, const File& b) {
    return a.extents[0].start_block() < b.extents[0].start_block();
  });

  if (is_zlib && extract_deflates) {
    // If it is infact gzipped, then the sqfs_path should be valid to read its
    // content.
    TEST_AND_RETURN_FALSE(!sqfs_path.empty());
    if (zlib_blks.empty()) {
      return true;
    }

    // Sort zlib blocks.
    std::sort(zlib_blks.begin(), zlib_blks.end(),
              [](const puffin::ByteExtent& a, const puffin::ByteExtent& b) {
                return a.offset < b.offset;
              });

    // Sometimes a squashfs can have a two files that are hard linked. In this
    // case both files will have the same starting offset in the image and hence
    // the same zlib blocks. So we need to remove these duplicates to eliminate
    // further potential probems. As a matter of fact the next statement will
    // fail if there are duplicates (there will be overlap between two blocks).
    auto last = std::unique(zlib_blks.begin(), zlib_blks.end());
    zlib_blks.erase(last, zlib_blks.end());

    // Make sure zlib blocks are not overlapping.
    auto result = std::adjacent_find(
        zlib_blks.begin(), zlib_blks.end(),
        [](const puffin::ByteExtent& a, const puffin::ByteExtent& b) {
          return (a.offset + a.length) > b.offset;
        });
    TEST_AND_RETURN_FALSE(result == zlib_blks.end());

    vector<puffin::BitExtent> deflates;
    TEST_AND_RETURN_FALSE(
        puffin::LocateDeflatesInZlibBlocks(sqfs_path, zlib_blks, &deflates));

    // Add deflates for each file.
    for (auto& file : files_) {
      file.deflates = deflate_utils::FindDeflates(file.extents, deflates);
    }
  }
  return true;
}

unique_ptr<SquashfsFilesystem> SquashfsFilesystem::CreateFromFile(
    const string& sqfs_path, bool extract_deflates, bool load_settings) {
  if (sqfs_path.empty()) {
    return nullptr;
  }

  brillo::StreamPtr sqfs_file = brillo::FileStream::Open(
      FilePath(sqfs_path), brillo::Stream::AccessMode::READ,
      brillo::FileStream::Disposition::OPEN_EXISTING, nullptr);
  if (!sqfs_file) {
    LOG(ERROR) << "Unable to open " << sqfs_path << " for reading.";
    return nullptr;
  }

  SquashfsHeader header;
  brillo::Blob blob(kSquashfsSuperBlockSize);
  if (!sqfs_file->ReadAllBlocking(blob.data(), blob.size(), nullptr)) {
    LOG(ERROR) << "Unable to read from file: " << sqfs_path;
    return nullptr;
  }
  if (!ReadSquashfsHeader(blob, &header) || !CheckHeader(header)) {
    // This is not necessary an error.
    return nullptr;
  }

  // Read the map file.
  string filemap;
  if (!GetFileMapContent(sqfs_path, &filemap)) {
    LOG(ERROR) << "Failed to produce squashfs map file: " << sqfs_path;
    return nullptr;
  }

  unique_ptr<SquashfsFilesystem> sqfs(new SquashfsFilesystem());
  if (!sqfs->Init(filemap, sqfs_path, sqfs_file->GetSize(), header,
                  extract_deflates)) {
    LOG(ERROR) << "Failed to initialized the Squashfs file system";
    return nullptr;
  }

  if (load_settings) {
    if (!GetUpdateEngineConfig(sqfs_path, &sqfs->update_engine_config_)) {
      return nullptr;
    }
  }

  return sqfs;
}

unique_ptr<SquashfsFilesystem> SquashfsFilesystem::CreateFromFileMap(
    const string& filemap, size_t size, const SquashfsHeader& header) {
  if (!CheckHeader(header)) {
    LOG(ERROR) << "Invalid Squashfs super block!";
    return nullptr;
  }

  unique_ptr<SquashfsFilesystem> sqfs(new SquashfsFilesystem());
  if (!sqfs->Init(filemap, "", size, header, false)) {
    LOG(ERROR) << "Failed to initialize the Squashfs file system using filemap";
    return nullptr;
  }
  // TODO(ahassani): Add a function that initializes the puffin related extents.
  return sqfs;
}

size_t SquashfsFilesystem::GetBlockSize() const {
  return kBlockSize;
}

size_t SquashfsFilesystem::GetBlockCount() const {
  return size_ / kBlockSize;
}

bool SquashfsFilesystem::GetFiles(vector<File>* files) const {
  files->insert(files->end(), files_.begin(), files_.end());
  return true;
}

bool SquashfsFilesystem::LoadSettings(brillo::KeyValueStore* store) const {
  if (!store->LoadFromString(update_engine_config_)) {
    LOG(ERROR) << "Failed to load the settings with config: "
               << update_engine_config_;
    return false;
  }
  return true;
}

bool SquashfsFilesystem::IsSquashfsImage(const brillo::Blob& blob) {
  SquashfsHeader header;
  return ReadSquashfsHeader(blob, &header) && CheckHeader(header);
}
}  // namespace chromeos_update_engine
