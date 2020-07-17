// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// anomaly_detector examines the log files, namely /var/log/messages,
// /var/log/upstart.log, and /var/log/audit/audit.log, using
// anomaly::LogReader and looks for messages matching particular patterns. When
// it finds one, it invokes crash_reporter appropriately to report the issue.

// This file (and the associated .cc file) contains logic to parse log
// entries and determine whether to invoke crash_reporter (or how to invoke it).
// The logic to read from plaintext files lives in
// anomaly_detector_text_file_reader.h and anomaly_detector_log_reader.h. The
// logic to setup LogReader, pass entries to corresponding parser and execute
// crash_reporter lives in anomaly_detector_main.cc.

#ifndef CRASH_REPORTER_ANOMALY_DETECTOR_H_
#define CRASH_REPORTER_ANOMALY_DETECTOR_H_

#include <base/optional.h>
#include <base/time/time.h>
#include <dbus/bus.h>

#include <string>
#include <vector>

#include <inttypes.h>

namespace anomaly {

struct CrashReport {
  std::string text;
  std::string flag;
};

using MaybeCrashReport = base::Optional<CrashReport>;

constexpr size_t HASH_BITMAP_SIZE(1 << 15);

class Parser {
 public:
  virtual ~Parser() = 0;

  virtual MaybeCrashReport ParseLogEntry(const std::string& line) = 0;

  virtual bool WasAlreadySeen(uint32_t hash);

  // Called once every 10-20 seconds to allow Parser to update state in ways
  // that aren't always tied to receiving a message.
  virtual void PeriodicUpdate();

 protected:
  enum class LineType {
    None,
    Header,
    Start,
    Body,
  };

 private:
  std::bitset<HASH_BITMAP_SIZE> hash_bitmap_;
};

class ServiceParser : public Parser {
 public:
  explicit ServiceParser(bool testonly_send_all);

  MaybeCrashReport ParseLogEntry(const std::string& line) override;

 private:
  const bool testonly_send_all_;
};

class SELinuxParser : public Parser {
 public:
  explicit SELinuxParser(bool testonly_send_all);
  MaybeCrashReport ParseLogEntry(const std::string& line) override;

 private:
  const bool testonly_send_all_;
};

class KernelParser : public Parser {
 public:
  MaybeCrashReport ParseLogEntry(const std::string& line) override;

 private:
  LineType last_line_ = LineType::None;
  std::string text_;
  std::string flag_;

  // Timestamp of last time crash_reporter failed.
  base::TimeTicks crash_reporter_last_crashed_ = base::TimeTicks();
};

class SuspendParser : public Parser {
 public:
  MaybeCrashReport ParseLogEntry(const std::string& line) override;

 private:
  LineType last_line_ = LineType::None;
  std::string dev_str_;
  std::string errno_str_;
  std::string step_str_;
};

class TerminaParser {
 public:
  explicit TerminaParser(scoped_refptr<dbus::Bus> dbus);
  MaybeCrashReport ParseLogEntry(const std::string& tag,
                                 const std::string& line);

 private:
  scoped_refptr<dbus::Bus> dbus_;
};

}  // namespace anomaly

#endif  // CRASH_REPORTER_ANOMALY_DETECTOR_H_
