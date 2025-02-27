// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/file_writer.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <string>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>

#include "update_engine/common/test_utils.h"
#include "update_engine/common/utils.h"

using std::string;

namespace chromeos_update_engine {

class FileWriterTest : public ::testing::Test {};

TEST(FileWriterTest, SimpleTest) {
  ScopedTempFile file("FileWriterTest-XXXXXX");
  DirectFileWriter file_writer;
  EXPECT_EQ(0,
            file_writer.Open(file.path().c_str(),
                             O_CREAT | O_LARGEFILE | O_TRUNC | O_WRONLY, 0644));
  EXPECT_TRUE(file_writer.Write("test", 4));
  brillo::Blob actual_data;
  EXPECT_TRUE(utils::ReadFile(file.path(), &actual_data));

  EXPECT_EQ("test", string(actual_data.begin(), actual_data.end()));
  EXPECT_EQ(0, file_writer.Close());
}

TEST(FileWriterTest, ErrorTest) {
  DirectFileWriter file_writer;
  const string path("/tmp/ENOENT/FileWriterTest");
  EXPECT_EQ(-ENOENT, file_writer.Open(path.c_str(),
                                      O_CREAT | O_LARGEFILE | O_TRUNC, 0644));
}

TEST(FileWriterTest, WriteErrorTest) {
  // Create a uniquely named file for testing.
  ScopedTempFile file("FileWriterTest-XXXXXX");
  DirectFileWriter file_writer;
  EXPECT_EQ(0,
            file_writer.Open(file.path().c_str(),
                             O_CREAT | O_LARGEFILE | O_TRUNC | O_RDONLY, 0644));
  EXPECT_FALSE(file_writer.Write("x", 1));
  EXPECT_EQ(0, file_writer.Close());
}

}  // namespace chromeos_update_engine
