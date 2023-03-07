// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "libhwsec/backend/tpm2/backend_test_base.h"

using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::DoAll;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::SetArgPointee;
using tpm_manager::NvramResult;
using tpm_manager::NvramSpaceAttribute;
using tpm_manager::TpmManagerStatus;

namespace {
constexpr uint32_t kFwmpIndex = 0x100a;
constexpr uint32_t kInstallAttributesIndex =
    USE_TPM_DYNAMIC ? 0x9da5b0 : 0x800004;
constexpr uint32_t kEnterpriseRollbackIndex = 0x100e;
}  // namespace

namespace hwsec {

using BackendStorageTpm2Test = BackendTpm2TestBase;

TEST_F(BackendStorageTpm2Test, IsReady) {
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  list_reply.add_index_list(kInstallAttributesIndex);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::RemoveOwnerDependencyReply remove_reply;
  remove_reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(),
              RemoveOwnerDependency(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(remove_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().IsReady(Space::kInstallAttributes),
              IsOkAndHolds(Backend::Storage::ReadyState::kReadableAndWritable));
}

TEST_F(BackendStorageTpm2Test, IsReadyPreparable) {
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  list_reply.add_index_list(kInstallAttributesIndex);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::GetTpmNonsensitiveStatusReply status_reply;
  status_reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  status_reply.set_is_enabled(true);
  status_reply.set_is_owned(true);
  status_reply.set_is_owner_password_present(true);
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(),
              GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(status_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().IsReady(Space::kInstallAttributes),
              IsOkAndHolds(Backend::Storage::ReadyState::kPreparable));
}

TEST_F(BackendStorageTpm2Test, IsReadyNotAvailable) {
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  list_reply.add_index_list(kInstallAttributesIndex);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::GetTpmNonsensitiveStatusReply status_reply;
  status_reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  status_reply.set_is_enabled(true);
  status_reply.set_is_owned(true);
  status_reply.set_is_owner_password_present(false);
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(),
              GetTpmNonsensitiveStatus(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(status_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().IsReady(Space::kInstallAttributes),
              NotOk());
}

TEST_F(BackendStorageTpm2Test, Prepare) {
  const uint32_t kFakeSize = 32;
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  list_reply.add_index_list(kInstallAttributesIndex);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::DestroySpaceReply destroy_reply;
  destroy_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), DestroySpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(destroy_reply), Return(true)));

  tpm_manager::DefineSpaceReply define_reply;
  define_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), DefineSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(define_reply), Return(true)));

  tpm_manager::RemoveOwnerDependencyReply remove_reply;
  remove_reply.set_status(TpmManagerStatus::STATUS_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmManagerProxy(),
              RemoveOwnerDependency(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(remove_reply), Return(true)));

  EXPECT_THAT(
      backend_->GetStorageTpm2().Prepare(Space::kInstallAttributes, kFakeSize),
      IsOk());
}

TEST_F(BackendStorageTpm2Test, PrepareNotAvailable) {
  const uint32_t kFakeSize = 32;
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  auto result = backend_->GetStorageTpm2().Prepare(
      Space::kPlatformFirmwareManagementParameters, kFakeSize);
  ASSERT_NOT_OK(result);
}

TEST_F(BackendStorageTpm2Test, PrepareReady) {
  const uint32_t kFakeSize = 32;
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  list_reply.add_index_list(kFwmpIndex);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(kFakeSize);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PLATFORM_CREATE);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_OWNER_WRITE);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PLATFORM_READ);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().Prepare(
                  Space::kPlatformFirmwareManagementParameters, kFakeSize),
              IsOk());
}

TEST_F(BackendStorageTpm2Test, Load) {
  const std::string kFakeData = "fake_data";

  tpm_manager::ReadSpaceReply read_reply;
  read_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  read_reply.set_data(kFakeData);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ReadSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(read_reply), Return(true)));

  EXPECT_THAT(
      backend_->GetStorageTpm2().Load(Space::kFirmwareManagementParameters),
      IsOkAndHolds(brillo::BlobFromString(kFakeData)));
}

TEST_F(BackendStorageTpm2Test, Store) {
  const std::string kFakeData = "fake_data";

  tpm_manager::WriteSpaceReply write_reply;
  write_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), WriteSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(write_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::LockSpaceReply lock_reply;
  lock_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), LockSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(lock_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().Store(
                  Space::kInstallAttributes, brillo::BlobFromString(kFakeData)),
              IsOk());
}

TEST_F(BackendStorageTpm2Test, Lock) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_BOOT_WRITE_LOCK);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_WRITE_AUTHORIZATION);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::LockSpaceReply lock_reply;
  lock_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), LockSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(lock_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().Lock(Space::kBootlockbox,
                                              Backend::Storage::LockOptions{
                                                  .read_lock = false,
                                                  .write_lock = true,
                                              }),
              IsOk());
}

TEST_F(BackendStorageTpm2Test, LockNoOp) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_BOOT_WRITE_LOCK);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_WRITE_AUTHORIZATION);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  tpm_manager::LockSpaceReply lock_reply;
  lock_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  // Space is already locked as requested, so no need to send the LockSpace
  // command again.
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), LockSpace).Times(0);

  EXPECT_THAT(backend_->GetStorageTpm2().Lock(Space::kBootlockbox,
                                              Backend::Storage::LockOptions{
                                                  .read_lock = false,
                                                  .write_lock = true,
                                              }),
              IsOk());
}

TEST_F(BackendStorageTpm2Test, IsWriteLocked) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(10);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(true);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(
      backend_->GetStorageTpm2().IsWriteLocked(Space::kInstallAttributes),
      IsOkAndHolds(true));
}

TEST_F(BackendStorageTpm2Test, EnterpriseRollbackReady) {
  const uint32_t kFakeSize = 32;
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  list_reply.add_index_list(kEnterpriseRollbackIndex);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(kFakeSize);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PLATFORM_CREATE);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_WRITE_AUTHORIZATION);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().IsReady(Space::kEnterpriseRollback),
              IsOkAndHolds(Storage::ReadyState::kReadableAndWritable));
}

TEST_F(BackendStorageTpm2Test, EnterpriseRollbackNotReady) {
  tpm_manager::ListSpacesReply list_reply;
  list_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ListSpaces(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(list_reply), Return(true)));

  EXPECT_THAT(backend_->GetStorageTpm2().IsReady(Space::kEnterpriseRollback),
              NotOk());
}

}  // namespace hwsec
