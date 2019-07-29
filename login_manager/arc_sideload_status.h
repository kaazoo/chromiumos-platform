// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_ARC_SIDELOAD_STATUS_H_
#define LOGIN_MANAGER_ARC_SIDELOAD_STATUS_H_

#include "login_manager/arc_sideload_status_interface.h"

#ifndef USE_CHEETS
#error "This file should only be used if cheets is used"
#endif

#include <memory>
#include <queue>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>

namespace dbus {
class ObjectProxy;
class Response;
}  // namespace dbus

namespace login_manager {

class ArcSideloadStatus : public ArcSideloadStatusInterface {
 public:
  explicit ArcSideloadStatus(dbus::ObjectProxy* boot_lockbox_proxy);
  virtual ~ArcSideloadStatus();

  // Overridden from ArcSideloadStatusInterface
  void Initialize() override;
  bool IsAdbSideloadAllowed() override;
  void EnableAdbSideload(EnableAdbSideloadCallback callback) override;
  void QueryAdbSideload(QueryAdbSideloadCallback callback) override;

  void OverrideAdbSideloadStatusTestOnly(bool allowed);

  // Requests boot attribute from bootlockbox. Public for test.
  void GetAdbSideloadAllowed();

 private:
  // Status enums for ADB Sideload status. Test only.
  enum SideloadStatus {
    ADB_SIDELOAD_UNKNOWN = 0,
    ADB_SIDELOAD_ALLOWED = 1,
    ADB_SIDELOAD_DISALLOWED = 2
  };

  // Called when the boot lockbox service becomes initially available.
  void OnBootLockboxServiceAvailable(bool service_available);

  // The response to GetBootAttribute is processed here.
  void OnGotAdbSideloadAllowed(dbus::Response* response);

  // The response to SetBootAttribute is processed here.
  void OnEnableAdbSideloadSet(EnableAdbSideloadCallback callback,
                              dbus::Response* result);

  // Parse response of ReadBootLockbox. Return true if sideload means to be
  // enabled.
  bool ParseResponseFromRead(dbus::Response* response);

  // Update the ADB sideload status based on information from cryptohome, and
  // run any pending query responses.
  void SetAdbSideloadStatusAndNotify(SideloadStatus status);

  // Issues respone for ARC sideload status requests via DBus.
  void SendQueryAdbSideloadResponse(QueryAdbSideloadCallback callback);

  dbus::ObjectProxy* boot_lockbox_proxy_;  // Owned by the caller.

  SideloadStatus sideload_status_;
  std::queue<QueryAdbSideloadCallback> query_arc_sideload_callback_queue_;

  base::WeakPtrFactory<ArcSideloadStatus> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ArcSideloadStatus);
};

}  // namespace login_manager

#endif  // LOGIN_MANAGER_ARC_SIDELOAD_STATUS_H_
