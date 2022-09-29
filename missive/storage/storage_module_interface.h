// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_STORAGE_STORAGE_MODULE_INTERFACE_H_
#define MISSIVE_STORAGE_STORAGE_MODULE_INTERFACE_H_

#include <base/callback.h>
#include <base/memory/ref_counted.h>
#include <base/memory/scoped_refptr.h>

#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"

namespace reporting {

class StorageModuleInterface
    : public base::RefCountedThreadSafe<StorageModuleInterface> {
 public:
  using EnqueueCallback = base::OnceCallback<void(Status)>;
  using FlushCallback = base::OnceCallback<void(Status)>;

  StorageModuleInterface(const StorageModuleInterface& other) = delete;
  StorageModuleInterface& operator=(const StorageModuleInterface& other) =
      delete;

  // AddRecord will add |record| (taking ownership) to the
  // |StorageModuleInterface| according to the provided |priority|. On
  // completion, |callback| is called.
  virtual void AddRecord(Priority priority,
                         Record record,
                         EnqueueCallback callback) = 0;

  // Initiates upload of collected records according to the priority.
  // Called usually for a queue with an infinite or very large upload period.
  // Multiple |Flush| calls can safely run in parallel.
  // Returns error if cannot start upload.
  virtual void Flush(Priority priority, FlushCallback callback) = 0;

  virtual base::StringPiece GetPipelineId() const {
    NOTREACHED();
    return "Should never reach this line.";
  }

 protected:
  // Constructor can only be called by |Create| factory method.
  StorageModuleInterface();

  // Refcounted object must have destructor declared protected or private.
  virtual ~StorageModuleInterface();

 private:
  friend base::RefCountedThreadSafe<StorageModuleInterface>;
};

}  // namespace reporting

#endif  // MISSIVE_STORAGE_STORAGE_MODULE_INTERFACE_H_
