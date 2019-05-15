// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_SERVICE_USERDATAAUTH_H_
#define CRYPTOHOME_SERVICE_USERDATAAUTH_H_

#include <memory>
#include <string>

#include <dbus/cryptohome/dbus-constants.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>

#include "cryptohome/userdataauth.h"
#include "dbus_adaptors/org.chromium.UserDataAuth.h"

namespace cryptohome {
class UserDataAuthAdaptor
    : public org::chromium::UserDataAuthInterfaceInterface,
      public org::chromium::UserDataAuthInterfaceAdaptor {
 public:
  explicit UserDataAuthAdaptor(scoped_refptr<dbus::Bus> bus,
                               brillo::dbus_utils::DBusObject* dbus_object,
                               UserDataAuth* service)
      : org::chromium::UserDataAuthInterfaceAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  // Note that the documentation for all of the methods below can be found in
  // either the DBus Introspection XML
  // (cryptohome/dbus_bindings/org.chromium.UserDataAuth.xml), or the protobuf
  // definition file (system_api/dbus/cryptohome/UserDataAuth.proto)
  void IsMounted(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     user_data_auth::IsMountedReply>> response,
                 const user_data_auth::IsMountedRequest& in_request) override;
  void DoIsMounted(const std::string username,
                   std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       user_data_auth::IsMountedReply>> response);

  void Unmount(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   user_data_auth::UnmountReply>> response,
               const user_data_auth::UnmountRequest& in_request) override;
  void DoUnmount(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     user_data_auth::UnmountReply>> response);

  void Mount(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                 user_data_auth::MountReply>> response,
             const user_data_auth::MountRequest& in_request) override;
  void DoMount(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                   user_data_auth::MountReply>> response,
               const user_data_auth::MountRequest& in_request);
  void Remove(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                  user_data_auth::RemoveReply>> response,
              const user_data_auth::RemoveRequest& in_request) override;
  void Rename(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                  user_data_auth::RenameReply>> response,
              const user_data_auth::RenameRequest& in_request) override;
  void ListKeys(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                    user_data_auth::ListKeysReply>> response,
                const user_data_auth::ListKeysRequest& in_request) override;
  void GetKeyData(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                      user_data_auth::GetKeyDataReply>> response,
                  const user_data_auth::GetKeyDataRequest& in_request) override;
  void CheckKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                    user_data_auth::CheckKeyReply>> response,
                const user_data_auth::CheckKeyRequest& in_request) override;
  void DoCheckKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                      user_data_auth::CheckKeyReply>> response,
                  const user_data_auth::CheckKeyRequest& in_request);

  void AddKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                  user_data_auth::AddKeyReply>> response,
              const user_data_auth::AddKeyRequest& in_request) override;
  void DoAddKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                    user_data_auth::AddKeyReply>> response,
                const user_data_auth::AddKeyRequest& in_request);

  void UpdateKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     user_data_auth::UpdateKeyReply>> response,
                 const user_data_auth::UpdateKeyRequest& in_request) override;

  void RemoveKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                     user_data_auth::RemoveKeyReply>> response,
                 const user_data_auth::RemoveKeyRequest& in_request) override;
  void DoRemoveKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                       user_data_auth::RemoveKeyReply>> response,
                   const user_data_auth::RemoveKeyRequest& in_request);

  void MigrateKey(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                      user_data_auth::MigrateKeyReply>> response,
                  const user_data_auth::MigrateKeyRequest& in_request) override;
  void StartMigrateToDircrypto(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::StartMigrateToDircryptoReply>> response,
      const user_data_auth::StartMigrateToDircryptoRequest& in_request)
      override;
  void NeedsDircryptoMigration(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::NeedsDircryptoMigrationReply>> response,
      const user_data_auth::NeedsDircryptoMigrationRequest& in_request)
      override;
  void GetSupportedKeyPolicies(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetSupportedKeyPoliciesReply>> response,
      const user_data_auth::GetSupportedKeyPoliciesRequest& in_request)
      override;
  void GetAccountDiskUsage(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetAccountDiskUsageReply>> response,
      const user_data_auth::GetAccountDiskUsageRequest& in_request) override;

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  DISALLOW_COPY_AND_ASSIGN(UserDataAuthAdaptor);
};

class ArcQuotaAdaptor : public org::chromium::ArcQuotaInterface,
                        public org::chromium::ArcQuotaAdaptor {
 public:
  explicit ArcQuotaAdaptor(scoped_refptr<dbus::Bus> bus,
                           brillo::dbus_utils::DBusObject* dbus_object,
                           UserDataAuth* service)
      : org::chromium::ArcQuotaAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  // Note that the documentation for all of the methods below can be found in
  // either the DBus Introspection XML
  // (cryptohome/dbus_bindings/org.chromium.UserDataAuth.xml), or the protobuf
  // definition file (system_api/dbus/cryptohome/UserDataAuth.proto)
  void GetArcDiskFeatures(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetArcDiskFeaturesReply>> response,
      const user_data_auth::GetArcDiskFeaturesRequest& in_request) override;
  void GetCurrentSpaceForArcUid(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetCurrentSpaceForArcUidReply>> response,
      const user_data_auth::GetCurrentSpaceForArcUidRequest& in_request)
      override;
  void GetCurrentSpaceForArcGid(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetCurrentSpaceForArcGidReply>> response,
      const user_data_auth::GetCurrentSpaceForArcGidRequest& in_request)
      override;

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  DISALLOW_COPY_AND_ASSIGN(ArcQuotaAdaptor);
};

class Pkcs11Adaptor : public org::chromium::CryptohomePkcs11InterfaceInterface,
                      public org::chromium::CryptohomePkcs11InterfaceAdaptor {
 public:
  explicit Pkcs11Adaptor(scoped_refptr<dbus::Bus> bus,
                         brillo::dbus_utils::DBusObject* dbus_object,
                         UserDataAuth* service)
      : org::chromium::CryptohomePkcs11InterfaceAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  void Pkcs11IsTpmTokenReady(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11IsTpmTokenReadyReply>> response,
      const user_data_auth::Pkcs11IsTpmTokenReadyRequest& in_request) override;
  void Pkcs11GetTpmTokeInfo(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11GetTpmTokeInfoReply>> response,
      const user_data_auth::Pkcs11GetTpmTokeInfoRequest& in_request) override;
  void Pkcs11Terminate(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::Pkcs11TerminateReply>> response,
      const user_data_auth::Pkcs11TerminateRequest& in_request) override;

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  DISALLOW_COPY_AND_ASSIGN(Pkcs11Adaptor);
};

class InstallAttributesAdaptor
    : public org::chromium::InstallAttributesInterfaceInterface,
      public org::chromium::InstallAttributesInterfaceAdaptor {
 public:
  explicit InstallAttributesAdaptor(scoped_refptr<dbus::Bus> bus,
                                    brillo::dbus_utils::DBusObject* dbus_object,
                                    UserDataAuth* service)
      : org::chromium::InstallAttributesInterfaceAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  void InstallAttributesGet(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::InstallAttributesGetReply>> response,
      const user_data_auth::InstallAttributesGetRequest& in_request) override;
  void InstallAttributesSet(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::InstallAttributesSetReply>> response,
      const user_data_auth::InstallAttributesSetRequest& in_request) override;
  void InstallAttributesFinalize(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::InstallAttributesFinalizeReply>> response,
      const user_data_auth::InstallAttributesFinalizeRequest& in_request)
      override;
  void InstallAttributesGetStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::InstallAttributesGetStatusReply>> response,
      const user_data_auth::InstallAttributesGetStatusRequest& in_request)
      override;
  void GetFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetFirmwareManagementParametersReply>> response,
      const user_data_auth::GetFirmwareManagementParametersRequest& in_request)
      override;
  void RemoveFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::RemoveFirmwareManagementParametersReply>> response,
      const user_data_auth::RemoveFirmwareManagementParametersRequest&
          in_request) override;
  void SetFirmwareManagementParameters(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::SetFirmwareManagementParametersReply>> response,
      const user_data_auth::SetFirmwareManagementParametersRequest& in_request)
      override;

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  DISALLOW_COPY_AND_ASSIGN(InstallAttributesAdaptor);
};

class CryptohomeMiscAdaptor
    : public org::chromium::CryptohomeMiscInterfaceInterface,
      public org::chromium::CryptohomeMiscInterfaceAdaptor {
 public:
  explicit CryptohomeMiscAdaptor(scoped_refptr<dbus::Bus> bus,
                                 brillo::dbus_utils::DBusObject* dbus_object,
                                 UserDataAuth* service)
      : org::chromium::CryptohomeMiscInterfaceAdaptor(this),
        dbus_object_(dbus_object),
        service_(service) {
    // This is to silence the compiler's warning about unused fields. It will be
    // removed once we start to use it.
    (void)service_;
  }

  void RegisterAsync() { RegisterWithDBusObject(dbus_object_); }

  // Interface overrides and related implementations
  void GetSystemSalt(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetSystemSaltReply>> response,
      const user_data_auth::GetSystemSaltRequest& in_request) override;
  void UpdateCurrentUserActivityTimestamp(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::UpdateCurrentUserActivityTimestampReply>> response,
      const user_data_auth::UpdateCurrentUserActivityTimestampRequest&
          in_request) override;
  void GetSanitizedUsername(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetSanitizedUsernameReply>> response,
      const user_data_auth::GetSanitizedUsernameRequest& in_request) override;
  void GetLoginStatus(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetLoginStatusReply>> response,
      const user_data_auth::GetLoginStatusRequest& in_request) override;
  void GetStatusString(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          user_data_auth::GetStatusStringReply>> response,
      const user_data_auth::GetStatusStringRequest& in_request) override;

 private:
  brillo::dbus_utils::DBusObject* dbus_object_;

  // This is the object that holds most of the states that this adaptor uses,
  // it also contains most of the actual logics.
  // This object is owned by the parent dbus service daemon, and whose lifetime
  // will cover the entire lifetime of this class.
  UserDataAuth* service_;

  DISALLOW_COPY_AND_ASSIGN(CryptohomeMiscAdaptor);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_SERVICE_USERDATAAUTH_H_
