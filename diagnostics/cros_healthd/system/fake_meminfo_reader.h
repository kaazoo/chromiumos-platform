// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_MEMINFO_READER_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_MEMINFO_READER_H_

#include <optional>

#include "diagnostics/cros_healthd/system/meminfo_reader.h"

namespace diagnostics {

class FakeMeminfoReader : public MeminfoReader {
 public:
  FakeMeminfoReader();
  FakeMeminfoReader(const FakeMeminfoReader&) = delete;
  FakeMeminfoReader& operator=(const FakeMeminfoReader&) = delete;
  ~FakeMeminfoReader() override;

  std::optional<MemoryInfo> GetInfo() const override;

  void SetError(bool value);
  void SetTotalMemoryKib(uint64_t value);
  void SetFreeMemoryKib(uint64_t value);
  void SetAvailableMemoryKib(uint64_t value);
  void SetBuffersKib(uint64_t value);
  void SetPageCacheKib(uint64_t value);
  void SetSharedMemoryKib(uint64_t value);
  void SetActiveMemoryKib(uint64_t value);
  void SetInactiveMemoryKib(uint64_t value);
  void SetTotalSwapMemoryKib(uint64_t value);
  void SetFreeSwapMemoryKib(uint64_t value);
  void SetcachedSwapMemoryKib(uint64_t value);
  void SetTotalSlabMemoryKib(uint64_t value);
  void SetReclaimableSlabMemoryKib(uint64_t value);
  void SetUnreclaimableSlabMemoryKib(uint64_t value);

 private:
  bool is_error_ = false;
  uint64_t fake_total_memory_kib_ = 0;
  uint64_t fake_free_memory_kib_ = 0;
  uint64_t fake_available_memory_kib_ = 0;
  uint64_t fake_buffers_kib_ = 0;
  uint64_t fake_page_cache_kib_ = 0;
  uint64_t fake_shared_memory_kib_ = 0;
  uint64_t fake_active_memory_kib_ = 0;
  uint64_t fake_inactive_memory_kib_ = 0;
  uint64_t fake_total_swap_memory_kib_ = 0;
  uint64_t fake_free_swap_memory_kib_ = 0;
  uint64_t fake_cached_swap_memory_kib_ = 0;
  uint64_t fake_total_slab_memory_kib_ = 0;
  uint64_t fake_reclaimable_slab_memory_kib_ = 0;
  uint64_t fake_unreclaimable_slab_memory_kib_ = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_MEMINFO_READER_H_
