// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UTILS_H_
#define UTILS_H_

#include <map>
#include <set>
#include <vector>

#include "base/basictypes.h"

#include "kernel/perf_internals.h"
#include "quipper_string.h"

namespace quipper {

extern const char* kSupportedMetadata[];

build_id_event* CallocMemoryForBuildID(size_t size);

bool FileToBuffer(const string& filename, std::vector<char>* contents);

bool BufferToFile(const string& filename, const std::vector<char>& contents);

long int GetFileSize(const string& filename);

bool CompareFileContents(const string& file1, const string& file2);

uint64 Md5Prefix(const string& input);

bool CreateNamedTempFile(string* name);

// Returns true if the perf reports show the same summary.  Metadata
// is compared if it is present in kSupportedMetadata in utils.cc.
bool ComparePerfReportsByFields(const string& quipper_input,
                                const string& quipper_output,
                                const string& sort_fields);

// Default implementation of ComparePerfReportsByFields(), where |sort_fields|
// is set to a default value.
bool ComparePerfReports(const string& quipper_input,
                        const string& quipper_output);

// Similar to ComparePerfReports, but for piped perf data files.
// Warning: This is not commutative - |quipper_input| must be the piped perf
// data file passed to quipper, and |quipper_output| must be the file written
// by quipper.
bool ComparePipedPerfReports(const string& quipper_input,
                             const string& quipper_output,
                             std::set<string>* seen_metadata);

// Given a perf data file, get the list of build ids and create a map from
// filenames to build ids.
bool GetPerfBuildIDMap(const string& filename,
                       std::map<string, string>* output);

// Returns true if the perf buildid-lists are the same.
bool ComparePerfBuildIDLists(const string& file1, const string& file2);

// Returns a string that represents |array| in hexadecimal.
string HexToString(const u8* array, size_t length);

// Converts |str| to a hexadecimal number, stored in |array|.  Returns true on
// success.  Only stores up to |length| bytes - if there are more characters in
// the string, they are ignored (but the function may still return true).
bool StringToHex(const string& str, u8* array, size_t length);

// Adjust |size| to blocks of |align_size|.  i.e. returns the smallest multiple
// of |align_size| that can fit |size|.
uint64 AlignSize(uint64 size, uint32 align_size);

// Given a general perf sample format |sample_type|, return the fields of that
// format that are present in a sample for an event of type |event_type|.
//
// e.g. FORK and EXIT events have the fields {time, pid/tid, cpu, id}.
// Given a sample type with fields {ip, time, pid/tid, and period}, return
// the intersection of these two field sets: {time, pid/tid}.
//
// All field formats are bitfields, as defined by enum perf_event_sample_format
// in kernel/perf_event.h.
uint64 GetSampleFieldsForEventType(uint32 event_type, uint64 sample_type);

// Returns the offset in bytes within a perf event structure at which the raw
// perf sample data is located.
uint64 GetPerfSampleDataOffset(const event_t& event);

// Returns the size of the 8-byte-aligned memory for storing |string|.
size_t GetUint64AlignedStringLength(const string& str);

// Reads the contents of a file into |data|.  Returns true on success, false if
// it fails.
bool ReadFileToData(const string& filename, std::vector<char>* data);

// Writes contents of |data| to a file with name |filename|, overwriting any
// existing file.  Returns true on success, false if it fails.
bool WriteDataToFile(const std::vector<char>& data, const string& filename);

// Executes |command| and stores stdout output in |output|.  Returns true on
// success, false otherwise.
bool RunCommandAndGetStdout(const string& command, std::vector<char>* output);

}  // namespace quipper

#endif  // UTILS_H_
