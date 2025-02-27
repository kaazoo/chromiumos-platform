// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLATFORM_H_
#define SECAGENTD_PLATFORM_H_

// clang-format off
#include <bpf/libbpf.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <filesystem>
#include <vector>
// clang-format on

#include "base/files/file_descriptor_watcher_posix.h"
#include "base/functional/callback_forward.h"
#include "base/memory/weak_ptr.h"

namespace secagentd {

class PlatformInterface {
 public:
  virtual int IfNameToIndex(const std::string& ifname) = 0;
  virtual base::WeakPtr<PlatformInterface> GetWeakPtr() = 0;

  virtual int BpfMapDeleteElem(struct bpf_map*,
                               const void* key,
                               size_t key_sz,
                               __u64 flags) = 0;
  virtual int BpfMapUpdateElem(const struct bpf_map* map,
                               const void* key,
                               size_t key_sz,
                               const void* value,
                               size_t value_sz,
                               __u64 flags) = 0;
  virtual int BpfMapLookupElem(const struct bpf_map* map,
                               const void* key,
                               size_t key_sz,
                               void* value,
                               size_t value_sz,
                               __u64 flags) = 0;
  virtual int BpfMapGetNextKey(const struct bpf_map* map,
                               const void* cur_key,
                               void* next_key,
                               size_t key_sz) = 0;
  virtual int LibbpfSetStrictMode(enum libbpf_strict_mode mode) = 0;
  virtual int BpfObjectLoadSkeleton(struct bpf_object_skeleton* s) = 0;
  virtual int BpfObjectAttachSkeleton(struct bpf_object_skeleton* s) = 0;
  virtual void BpfObjectDetachSkeleton(struct bpf_object_skeleton* s) = 0;
  virtual void BpfObjectDestroySkeleton(struct bpf_object_skeleton* s) = 0;
  virtual int BpfMapFd(const struct bpf_map* map) = 0;
  virtual int BpfMapFdByName(struct bpf_object* obj,
                             const std::string name) = 0;
  virtual int BpfMapUpdateElementByFd(int fd,
                                      const void* key,
                                      const void* value,
                                      __u64 flags) = 0;
  virtual int BpfMapLookupElementByFd(int fd, const void* key, void* value) = 0;
  virtual int BpfMapDeleteElementByFd(int fd, const void* key) = 0;
  virtual struct ring_buffer* RingBufferNew(
      int map_fd,
      ring_buffer_sample_fn sample_cb,
      void* ctx,
      const struct ring_buffer_opts* opts) = 0;
  virtual int RingBufferEpollFd(const struct ring_buffer* rb) = 0;
  virtual int RingBufferConsume(struct ring_buffer* rb) = 0;
  virtual void RingBufferFree(struct ring_buffer* rb) = 0;
  virtual std::unique_ptr<base::FileDescriptorWatcher::Controller>
  WatchReadable(int fd, const base::RepeatingClosure& callback) = 0;
  virtual int Sys_statx(int dir_fd,
                        const std::string& path,
                        int flags,
                        unsigned int mask,
                        struct statx* statxbuf) = 0;
  virtual bool FilePathExists(const std::string& path) const = 0;
  virtual bool IsFilePathDirectory(const std::string& path) const = 0;
  virtual std::vector<std::filesystem::directory_entry>
  FileSystemDirectoryIterator(const std::string& path) const = 0;
  virtual int OpenDirectory(const std::string& path) const = 0;
  virtual int CloseDirectory(int fd) const = 0;
  virtual ~PlatformInterface() = default;
};

class Platform : public PlatformInterface {
 public:
  Platform() : weak_ptr_factory_(this) {}
  ~Platform() override = default;
  base::WeakPtr<PlatformInterface> GetWeakPtr() override;
  int IfNameToIndex(const std::string& ifname) override;

  int BpfMapDeleteElem(struct bpf_map*,
                       const void* key,
                       size_t key_sz,
                       __u64 flags) override;
  int BpfMapUpdateElem(const struct bpf_map* map,
                       const void* key,
                       size_t key_sz,
                       const void* value,
                       size_t value_sz,
                       __u64 flags) override;
  int BpfMapLookupElem(const struct bpf_map* map,
                       const void* key,
                       size_t key_sz,
                       void* value,
                       size_t value_sz,
                       __u64 flags) override;
  int BpfMapGetNextKey(const struct bpf_map* map,
                       const void* cur_key,
                       void* next_key,
                       size_t key_sz) override;
  int LibbpfSetStrictMode(enum libbpf_strict_mode mode) override;
  int BpfObjectLoadSkeleton(struct bpf_object_skeleton* s) override;
  int BpfObjectAttachSkeleton(struct bpf_object_skeleton* s) override;
  void BpfObjectDetachSkeleton(struct bpf_object_skeleton* s) override;
  void BpfObjectDestroySkeleton(struct bpf_object_skeleton* s) override;
  int BpfMapFd(const struct bpf_map* map) override;
  int BpfMapFdByName(struct bpf_object* obj, const std::string name) override;
  int BpfMapUpdateElementByFd(int fd,
                              const void* key,
                              const void* value,
                              __u64 flags) override;
  int BpfMapLookupElementByFd(int fd, const void* key, void* value) override;
  int BpfMapDeleteElementByFd(int fd, const void* key) override;
  struct ring_buffer* RingBufferNew(
      int map_fd,
      ring_buffer_sample_fn sample_cb,
      void* ctx,
      const struct ring_buffer_opts* opts) override;
  int RingBufferEpollFd(const struct ring_buffer* rb) override;
  int RingBufferConsume(struct ring_buffer* rb) override;
  void RingBufferFree(struct ring_buffer* rb) override;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> WatchReadable(
      int fd, const base::RepeatingClosure& callback) override;
  int Sys_statx(int dir_fd,
                const std::string& path,
                int flags,
                unsigned int mask,
                struct statx* statxbuf) override;
  bool FilePathExists(const std::string& path) const override;
  bool IsFilePathDirectory(const std::string& path) const override;
  std::vector<std::filesystem::directory_entry> FileSystemDirectoryIterator(
      const std::string& path) const override;
  int OpenDirectory(const std::string& path) const override;
  int CloseDirectory(int fd) const override;

 private:
  base::WeakPtrFactory<Platform> weak_ptr_factory_;
};

base::WeakPtr<PlatformInterface> GetPlatform();
base::WeakPtr<PlatformInterface> SetPlatform(
    std::unique_ptr<PlatformInterface> platform);

}  // namespace secagentd
#endif  // SECAGENTD_PLATFORM_H_
