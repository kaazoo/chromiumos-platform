// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_builder.h"

#include <utility>

#include "base/files/scoped_file.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace vm_tools::concierge {

TEST(VmBuilderTest, DefaultValuesSucceeds) {
  VmBuilder builder;
  EXPECT_FALSE(std::move(builder).BuildVmArgs(nullptr)->empty());
}

TEST(VmBuilderTest, CustomParametersWithCrosvmFlags) {
  CustomParametersForDev dev{R"(prerun:--log-level=debug)"};

  VmBuilder builder;
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[0].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[1].first, "--log-level");
  EXPECT_EQ(result[1].second, "debug");
  EXPECT_EQ(result[2].first, "run");
}

TEST(VmBuilderTest, CustomParametersWithSyslogTag) {
  CustomParametersForDev dev{R"(prerun:--log-level=debug)"};

  VmBuilder builder;
  builder.SetSyslogTag("TEST");
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[0].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[1].first, "--syslog-tag");
  EXPECT_EQ(result[1].second, "TEST");
  EXPECT_EQ(result[2].first, "--log-level");
  EXPECT_EQ(result[2].second, "debug");
  EXPECT_EQ(result[3].first, "run");
}

TEST(VmBuilderTest, CustomParametersWithStrace) {
  CustomParametersForDev dev{
      R"(precrosvm:/usr/local/bin/strace
precrosvm:-f
precrosvm:-o=/run/vm/crosvm_strace)"};

  VmBuilder builder;
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();
  EXPECT_EQ(result[0].first, "/usr/local/bin/strace");
  EXPECT_EQ(result[1].first, "-f");
  EXPECT_EQ(result[1].second, "");
  // We can't do preprocessing on the precrosvm arguments, so let it just pass
  // through.
  EXPECT_EQ(result[2].first, "-o=/run/vm/crosvm_strace");
  EXPECT_EQ(result[2].second, "");
  EXPECT_EQ(result[3].first, "/usr/bin/crosvm");
  EXPECT_EQ(result[4].first, "run");
}

TEST(VmBuilderTest, ODirectN) {
  CustomParametersForDev dev{R"(O_DIRECT_N=2)"};

  VmBuilder builder;
  builder.AppendDisks(std::vector<Disk>{
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  std::vector<std::string> disk_params;
  for (auto& p : result) {
    if (p.first == "--disk") {
      disk_params.push_back(p.second);
    }
  }

  EXPECT_EQ(disk_params[0], "/dev/zero");
  EXPECT_EQ(disk_params[1], "/dev/zero");
  EXPECT_EQ(disk_params[2], "/dev/zero,o_direct=true,block_size=4096");
}

TEST(VmBuilderTest, ODirectNs) {
  CustomParametersForDev dev{R"(O_DIRECT_N=1
O_DIRECT_N=2)"};

  VmBuilder builder;
  builder.AppendDisks(std::vector<Disk>{
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
      Disk{
          .path = base::FilePath("/dev/zero"),
      },
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  std::vector<std::string> disk_params;
  for (auto& p : result) {
    if (p.first == "--disk") {
      disk_params.push_back(p.second);
    }
  }

  EXPECT_EQ(disk_params[0], "/dev/zero");
  EXPECT_EQ(disk_params[1], "/dev/zero,o_direct=true,block_size=4096");
  EXPECT_EQ(disk_params[2], "/dev/zero,o_direct=true,block_size=4096");
}

TEST(VmBuilderTest, ODirectTooLargeNDeath) {
  CustomParametersForDev dev{R"(O_DIRECT_N=15)"};
  VmBuilder builder;
  ASSERT_DEATH(
      {
        base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();
      },
      "out_of_range");
}

TEST(VmBuilderTest, DefaultKernel) {
  VmBuilder builder;
  builder.SetKernel(base::FilePath("/dev/null"));
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_EQ(result[result.size() - 1].first, "/dev/null");
}

TEST(VmBuilderTest, CustomKernel) {
  CustomParametersForDev dev{R"(KERNEL_PATH=/dev/zero)"};

  VmBuilder builder;
  builder.SetKernel(base::FilePath("/dev/null"));
  base::StringPairs result = std::move(builder).BuildVmArgs(&dev).value();

  EXPECT_EQ(result[result.size() - 1].first, "/dev/zero");
}

TEST(VmBuilderTest, SingleTapNetParams) {
  base::ScopedFD fake_fd(open("/dev/zero", O_RDONLY));
  int raw_fd = fake_fd.get();

  VmBuilder builder;
  builder.AppendTapFd(std::move(fake_fd));
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_THAT(
      result,
      testing::Contains(
          std::make_pair("--net", base::StringPrintf(
                                      "packed-queue=true,tap-fd=%d", raw_fd)))
          .Times(1));
}

TEST(VmBuilderTest, MultipleTapNetParams) {
  base::ScopedFD fake_fd_1(open("/dev/zero", O_RDONLY));
  base::ScopedFD fake_fd_2(open("/dev/zero", O_RDONLY));
  int raw_fd_1 = fake_fd_1.get();
  int raw_fd_2 = fake_fd_2.get();

  VmBuilder builder;
  builder.AppendTapFd(std::move(fake_fd_1));
  builder.AppendTapFd(std::move(fake_fd_2));
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  EXPECT_THAT(
      result,
      testing::Contains(
          std::make_pair("--net", base::StringPrintf(
                                      "packed-queue=true,tap-fd=%d", raw_fd_1)))
          .Times(1));
  EXPECT_THAT(
      result,
      testing::Contains(
          std::make_pair("--net", base::StringPrintf(
                                      "packed-queue=true,tap-fd=%d", raw_fd_2)))
          .Times(1));
}

TEST(VmBuilderTest, CrostiniDisks) {
  VmBuilder builder;
  builder.AppendDisks(std::vector<Disk>{
      // For rootfs.
      Disk{
          .path = base::FilePath("/dev/0"),
      },
      // For user data.
      Disk{
          .path = base::FilePath("/dev/1"),
          .writable = true,
          .sparse = false,
      },
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  std::vector<int> disk_indices;
  for (int i = 0; i < result.size(); i++) {
    if (result[i].first == "--disk" || result[i].first == "--rwdisk") {
      disk_indices.push_back(i);
    }
  }

  EXPECT_EQ(disk_indices.size(), 2);
  EXPECT_EQ(result[disk_indices[0]], std::make_pair("--disk", "/dev/0"));
  EXPECT_EQ(result[disk_indices[1]],
            std::make_pair("--rwdisk", "/dev/1,sparse=false"));
}

TEST(VmBuilderTest, ARCVMDisks) {
  VmBuilder builder;
  builder.AppendDisks(std::vector<Disk>{
      // For system.img and vendor.img.
      Disk{
          .path = base::FilePath("/dev/0"),
          .o_direct = true,
          .block_size = 4096,
      },
      // For dummy fds.
      Disk{
          .path = base::FilePath("/dev/1"),
          .o_direct = false,
      },
      // For user data image.
      Disk{
          .path = base::FilePath("/dev/2"),
          .writable = true,
          .o_direct = true,
          .block_size = 4096,
      },
  });
  base::StringPairs result = std::move(builder).BuildVmArgs(nullptr).value();

  std::vector<int> disk_indices;
  for (int i = 0; i < result.size(); i++) {
    if (result[i].first == "--disk" || result[i].first == "--rwdisk") {
      disk_indices.push_back(i);
    }
  }

  EXPECT_EQ(disk_indices.size(), 3);
  EXPECT_EQ(result[disk_indices[0]],
            std::make_pair("--disk", "/dev/0,o_direct=true,block_size=4096"));
  EXPECT_EQ(result[disk_indices[1]],
            std::make_pair("--disk", "/dev/1,o_direct=false"));
  EXPECT_EQ(result[disk_indices[2]],
            std::make_pair("--rwdisk", "/dev/2,o_direct=true,block_size=4096"));
}

}  // namespace vm_tools::concierge
