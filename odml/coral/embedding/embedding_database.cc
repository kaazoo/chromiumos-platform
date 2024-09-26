// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/embedding/embedding_database.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>

#include "coral/proto_bindings/embedding.pb.h"

namespace coral {

std::unique_ptr<EmbeddingDatabase> EmbeddingDatabaseFactory::Create(
    const base::FilePath& file_path, const base::TimeDelta ttl) const {
  return EmbeddingDatabase::Create(file_path, ttl);
}

std::unique_ptr<EmbeddingDatabase> EmbeddingDatabase::Create(
    const base::FilePath& file_path, const base::TimeDelta ttl) {
  // EmbeddingDatabase() is private, so can not use make_unique.
  auto instance = base::WrapUnique(new EmbeddingDatabase(file_path, ttl));

  if (base::PathExists(file_path)) {
    // Do not return nullptr, since we can try overwriting the file later when
    // Sync().
    if (!instance->LoadFromFile()) {
      LOG(ERROR) << "Failed to load " << file_path;
    }
  } else {
    base::File::Error error;
    // If we can't create the parent directory, we can't write to |file_path|
    // later in Sync(). So return nullptr to indicate an error.
    if (!base::CreateDirectoryAndGetError(file_path.DirName(), &error)) {
      LOG(ERROR) << "Unable to create parent direcotry for " << file_path
                 << ": " << base::File::ErrorToString(error);
      return nullptr;
    } else {
      LOG(INFO) << "Created directory" << file_path.DirName();
    }
  }
  return instance;
}

EmbeddingDatabase::EmbeddingDatabase(const base::FilePath& file_path,
                                     const base::TimeDelta ttl)
    : dirty_(false), file_path_(file_path), ttl_(ttl) {}

EmbeddingDatabase::~EmbeddingDatabase() {
  // Ignore errors.
  Sync();
}

void EmbeddingDatabase::Put(std::string key, Embedding embedding) {
  // Overwrites existing keys, if any.
  EmbeddingRecord record;
  record.mutable_values()->Assign(embedding.begin(), embedding.end());
  record.set_updated_time_ms(base::Time::Now().InMillisecondsSinceUnixEpoch());
  embeddings_map_[std::move(key)] = std::move(record);
  dirty_ = true;
}

std::optional<Embedding> EmbeddingDatabase::Get(const std::string& key) {
  Embedding embedding;
  if (!embeddings_map_.contains(key)) {
    return std::nullopt;
  }
  // values are copied.
  embedding = Embedding(embeddings_map_[key].values().begin(),
                        embeddings_map_[key].values().end());
  embeddings_map_[key].set_updated_time_ms(
      base::Time::Now().InMillisecondsSinceUnixEpoch());
  dirty_ = true;
  return embedding;
}

bool EmbeddingDatabase::Sync() {
  // Remove stale records.
  base::Time now = base::Time::Now();

  int num_removed = 0;
  std::erase_if(embeddings_map_, [this, &now, &num_removed](const auto& entry) {
    if (IsRecordExpired(now, entry.second)) {
      ++num_removed;
      return true;
    }
    return false;
  });

  LOG(INFO) << "Sync " << file_path_ << " with now: " << now
            << ", ttl: " << ttl_ << ", num_removed: " << num_removed
            << ", size: " << embeddings_map_.size();

  if (!dirty_ && !num_removed) {
    return true;
  }

  EmbeddingRecords records;
  records.mutable_records()->insert(embeddings_map_.begin(),
                                    embeddings_map_.end());

  std::string buf;
  if (!records.SerializeToString(&buf)) {
    LOG(ERROR) << "Failed to seralize the to embeddings.";
    return false;
  }
  if (!base::WriteFile(file_path_, buf)) {
    LOG(ERROR) << "Failed to write embeddings to database.";
    return false;
  }
  return true;
}

bool EmbeddingDatabase::IsRecordExpired(const base::Time now,
                                        const EmbeddingRecord& record) const {
  base::Time last_used =
      base::Time::FromMillisecondsSinceUnixEpoch(record.updated_time_ms());
  // 0 means no ttl.
  return !ttl_.is_zero() && now - last_used > ttl_;
}

bool EmbeddingDatabase::LoadFromFile() {
  std::string buf;
  if (!base::ReadFileToString(file_path_, &buf)) {
    LOG(WARNING) << "Failed to read the embedding database.";
    return false;
  }

  EmbeddingRecords records;
  if (!records.ParseFromString(buf)) {
    LOG(ERROR) << "Failed to parse the embedding database at " << file_path_
               << ". Try removing the file.";
    if (!brillo::DeleteFile(file_path_)) {
      LOG(ERROR) << "Failed to delete the corrupted file " << file_path_;
    }
    return false;
  }

  // TODO(b/361429567) - Do not clean up stale records when loading the file.
  base::Time now = base::Time::Now();
  for (const auto& [key, record] : records.records()) {
    if (!IsRecordExpired(now, record)) {
      embeddings_map_[std::move(key)] = std::move(record);
    } else {
      // Some records are expired, so needs to sync to file.
      dirty_ = true;
    }
  }
  LOG(INFO) << "LoadFromFile " << file_path_ << " with now: " << now
            << ", ttl: " << ttl_ << ", num_removed: "
            << records.records().size() - embeddings_map_.size()
            << ", size: " << embeddings_map_.size();
  return true;
}

}  // namespace coral
