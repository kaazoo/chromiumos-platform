// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See //platform2/metrics/structured/README.md for more details.
#ifndef METRICS_STRUCTURED_LIB_KEY_DATA_H_
#define METRICS_STRUCTURED_LIB_KEY_DATA_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>

#include "metrics/structured/lib/persistent_proto.h"
#include "metrics/structured/lib/proto/key.pb.h"

namespace metrics::structured {

// KeyData is the central class for managing keys and generating hashes for
// structured metrics.
//
// The class maintains one key and its rotation data for every project defined
// in /tools/metrics/structured/sync/structured.xml. This can be used to
// generate:
//  - an ID for the project with KeyData::Id.
//  - a hash of a given value for an event with KeyData::HmacMetric.
//
// Every project has a uint64_t project_name_hash that is generated by taking
// the first 8 bytes of MD5 hash of the project name. Keys for the project are
// retrieved using this project_name_hash. For more details, refer to
// //tools/metrics/structured/ccodegen.py.
//
// KeyData performs key rotation. Every project is associated with a rotation
// period, which is 90 days unless specified in structured.xml. Keys are rotated
// with a resolution of one day. They are guaranteed not to be used for
// HmacMetric or UserProjectId for longer than their rotation period, except in
// cases of local clock changes.
//
// When first created, every project's key rotation date is selected uniformly
// so that there is an even distribution of rotations across users. This means
// that, for most users, the first rotation period will be shorter than the
// standard full rotation period for that project.
class KeyData {
 public:
  // Delegate to read and upsert keys.
  class StorageDelegate {
   public:
    virtual ~StorageDelegate() = default;

    // Returns if the delegate is ready to read or upsert keys.
    virtual bool IsReady() const = 0;

    // Returns the key associated with |project_name_hash|.
    //
    // If the key does not exist yet, then returns nullptr. Note that this will
    // return the expired key if it needs to be rotated.
    virtual const KeyProto* GetKey(uint64_t project_name_hash) const = 0;

    // Upserts the key for |project_name_hash| with duration
    // |key_rotation_period| and last updated time |last_key_rotation|.
    //
    // |last_key_rotation| is the TimeDelta from base::Time::UnixEpoch in
    // which the key was last rotated.
    virtual void UpsertKey(uint64_t project_name_hash,
                           base::TimeDelta last_key_rotation,
                           base::TimeDelta key_rotation_period) = 0;

    // Clears all key data.
    virtual void Purge() = 0;
  };

  // Key data will use |storage_delegate| to read and upsert keys.
  explicit KeyData(std::unique_ptr<StorageDelegate> storage_delegate);

  KeyData(const KeyData&) = delete;
  KeyData& operator=(const KeyData&) = delete;

  ~KeyData();

  // Returns a digest of |value| for |metric| in the context of
  // |project_name_hash|. Terminology: a metric is a (name, value) pair, and an
  // event is a bundle of metrics. Each event is associated with a project.
  //
  //  - |project_name_hash| is the uint64 name hash of a project.
  //  - |metric_name_hash| is the uint64 name hash of a metric.
  //  - |value| is the string value to hash.
  //
  // The result is the HMAC digest of the |value| salted with |metric|, using
  // the key for |project_name_hash|. That is:
  //
  //   HMAC_SHA256(key(project_name_hash), concat(value, hex(event),
  //   hex(metric)))
  //
  // Returns 0u in case of an error.
  //
  // TODO(b/316419439): Change |key_rotation_period| to base::TimeDelta.
  uint64_t HmacMetric(uint64_t project_name_hash,
                      uint64_t metric_name_hash,
                      const std::string& value,
                      int key_rotation_period);

  // Returns an ID for this (user, |project_name_hash|) pair.
  // |project_name_hash| is the name of a project, represented by the first 8
  // bytes of the MD5 hash of its name defined in structured.xml.
  //
  // The derived ID is the first 8 bytes of SHA256(key(project_name_hash)).
  // Returns 0u in case of an error.
  //
  // This ID is intended as the only ID for the events of a particular
  // structured metrics project. However, events are uploaded from the device
  // alongside the UMA client ID, which is only removed after the event reaches
  // the server. This means events are associated with the client ID when
  // uploaded from the device. See the class comment of
  // StructuredMetricsProvider for more details.
  //
  // Default |key_rotation_period| is 90 days.
  //
  // TODO(b/316419439): Change |key_rotation_period| to base::TimeDelta.
  uint64_t Id(uint64_t project_name_hash, int key_rotation_period);

  // Returns when the key for |project_name_hash| was last rotated, in days
  // since epoch. Returns nullopt if the key doesn't exist.
  //
  // TODO(b/316419439): Change |key_rotation_period| to base::TimeDelta.
  std::optional<int> LastKeyRotation(uint64_t project_name_hash) const;

  // Return the age of the key for |project_name_hash| since the last rotation,
  // in weeks.
  std::optional<int> GetKeyAgeInWeeks(uint64_t project_name_hash) const;

  // Clears all key data.
  void Purge();

 private:
  // Ensure that a valid key exists for |project|. If a key doesn't exist OR if
  // the key needs to be rotated, then a new key with |key_rotation_period| will
  // be created.
  //
  // This function assumes that |storage_delegate_->IsReady()| is true.
  void EnsureKeyUpdated(uint64_t project_name_hash,
                        base::TimeDelta key_rotation_period);

  // Retrieves the bytes of the key associated with |project_name_hash|.
  // If the key does not exist OR if the key is not of size |kKeySize|, returns
  // std::nullopt .
  const std::optional<std::string_view> GetKeyBytes(
      uint64_t project_name_hash) const;

  // Delegate that handles reading and upserting keys.
  std::unique_ptr<KeyData::StorageDelegate> storage_delegate_;
};

}  // namespace metrics::structured

#endif  // METRICS_STRUCTURED_LIB_KEY_DATA_H_
