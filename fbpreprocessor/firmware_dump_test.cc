// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>
#include <string>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <fbpreprocessor/proto_bindings/fbpreprocessor.pb.h>
#include <gtest/gtest.h>

#include "fbpreprocessor/firmware_dump.h"

namespace fbpreprocessor {
namespace {

class FirmwareDumpTest : public ::testing::Test {
 protected:
  void SetUp() override { CHECK(tmp_dir_.CreateUniqueTempDir()); }

  base::ScopedTempDir tmp_dir_;
};

TEST_F(FirmwareDumpTest, BaseNameSimple) {
  std::string name("test");
  FirmwareDump fw(tmp_dir_.GetPath().Append(name), FirmwareDump::Type::kWiFi);
  EXPECT_EQ(fw.BaseName(), base::FilePath(name));
}

TEST_F(FirmwareDumpTest, BaseNameDots) {
  std::string name("devcoredump_iwlwifi.20230901.231459.05766.1.gz");
  FirmwareDump fw(tmp_dir_.GetPath().Append(name), FirmwareDump::Type::kWiFi);
  EXPECT_EQ(fw.BaseName(), base::FilePath(name));
}

TEST_F(FirmwareDumpTest, DumpFileSimple) {
  std::string name("test");
  base::FilePath base_path(tmp_dir_.GetPath().Append(name));
  FirmwareDump fw(base_path, FirmwareDump::Type::kWiFi);
  EXPECT_EQ(fw.DumpFile(), base_path);
}

TEST_F(FirmwareDumpTest, DumpFileDots) {
  std::string name("devcoredump_iwlwifi.20230901.231459.05766.1");
  base::FilePath base_path(tmp_dir_.GetPath().Append(name));
  FirmwareDump fw(base_path, FirmwareDump::Type::kWiFi);
  EXPECT_EQ(fw.DumpFile(), base_path);
}

TEST_F(FirmwareDumpTest, DeleteRemovesFiles) {
  base::FilePath dmp(tmp_dir_.GetPath().Append("test"));

  base::WriteFile(dmp, "testdata");
  EXPECT_TRUE(base::PathExists(dmp));

  FirmwareDump fw(dmp, FirmwareDump::Type::kWiFi);
  EXPECT_TRUE(fw.Delete());
  // dmp file no longer exists.
  EXPECT_FALSE(base::PathExists(dmp));
}

TEST_F(FirmwareDumpTest, PrintToOStream) {
  // Verify the << operator.
  std::stringstream ss;
  FirmwareDump dump(base::FilePath("test.dmp"), FirmwareDump::Type::kWiFi);
  ss << dump;
  EXPECT_EQ(ss.str(), "test.dmp");
}

TEST_F(FirmwareDumpTest, VerifyWiFiFirmwareType) {
  FirmwareDump dump(base::FilePath("test.dmp"), FirmwareDump::Type::kWiFi);
  EXPECT_EQ(dump.type(), FirmwareDump::Type::kWiFi);
}

TEST_F(FirmwareDumpTest, VerifyWiFiFirmwareDBusTypeConversion) {
  EXPECT_EQ(FirmwareDump::ConvertToDBusType(FirmwareDump::Type::kWiFi),
            DebugDump::WIFI);
}

TEST_F(FirmwareDumpTest, VerifyBluetoothFirmwareType) {
  FirmwareDump dump(base::FilePath("test.dmp"), FirmwareDump::Type::kBluetooth);
  EXPECT_EQ(dump.type(), FirmwareDump::Type::kBluetooth);
}

TEST_F(FirmwareDumpTest, VerifyBluetoothFirmwareDBusTypeConversion) {
  EXPECT_EQ(FirmwareDump::ConvertToDBusType(FirmwareDump::Type::kBluetooth),
            DebugDump::BLUETOOTH);
}

}  // namespace
}  // namespace fbpreprocessor
