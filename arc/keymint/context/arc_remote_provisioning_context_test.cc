// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libarc-attestation/lib/test_utils.h>

#include "absl/strings/escaping.h"

namespace arc::keymint::context {

namespace {
using testing::NiceMock;

constexpr uint32_t kP256SignatureLength = 64;
constexpr uint32_t kOsVersion = 13000;
constexpr uint32_t kOsPatchLevel = 202407;
constexpr uint32_t kDeviceInfoMapVersion = 2;
constexpr uint32_t kSecureBootEnforced = 0;

constexpr char kEcdsaDERSignatureHex[] =
    "304402202183f1eec06a7eca46e676562d3e4f440741ad517a5387c45c54a69a9da846ef02"
    "205d3760585055de67ca94b0e2136380956b95b0a783eaae3d0070f1d3ff71782f";

constexpr char kDKCertPEM[] = R"(-----BEGIN CERTIFICATE-----
MIIDIzCCAgugAwIBAgIWAY90AREo6PnvDXoULHkAAAAAAFZJ/TANBgkqhkiG9w0B
AQsFADCBhTEgMB4GA1UEAxMXUHJpdmFjeSBDQSBJbnRlcm1lZGlhdGUxEjAQBgNV
BAsTCUNocm9tZSBPUzETMBEGA1UEChMKR29vZ2xlIEluYzEWMBQGA1UEBxMNTW91
bnRhaW4gVmlldzETMBEGA1UECBMKQ2FsaWZvcm5pYTELMAkGA1UEBhMCVVMwHhcN
MjQwNTIzMjExOTQ1WhcNNDQwNTIzMjExOTQ1WjBLMS8wLQYDVQQKEyZBUkMgUmVt
b3RlIEtleSBQcm92aXNpb25pbmcgRGV2aWNlIEtleTEYMBYGA1UECxMPc3RhdGU6
ZGV2ZWxvcGVyMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEv/vqwnEBQPTFFzx8
Zoh1G1UnHFHP44I/OfJgmNSXPMgWuG3DNmbjx37NdLMvZDdOCmGO9rBLW4mYGw+s
1G4rpqOBjDCBiTApBgNVHQ4EIgQgryr7Nm+PvuYDdg5kgj5m8kwpHvhRV6N+fBn5
1Kq1Jo0wKwYDVR0jBCQwIoAg9CC22dhi9osJFc6LV6T8V064wXyl+eZW29BSlCm9
bX8wDgYDVR0PAQH/BAQDAgeAMAwGA1UdEwEB/wQCMAAwEQYDVR0gBAowCDAGBgRV
HSAAMA0GCSqGSIb3DQEBCwUAA4IBAQCSGfeftmQYFmWXhtZlCo+Otf4HnUUH460F
uvSqrvnndWVvB0F5Q7ZFkGKnWQkBc/UIXLttBpcIme389VwR+U2OJ8HNc1+aaGiy
QUJHfFMcIyLatHMrlzeqNaLvnKM6oRipQyI9gBT+N28FtZFdHpY2HRXZV6e37T4N
MrJz6UCWQv8KVcVhXVKhXlnifgFcAUc3ci76vbNRaNAHcrEV9qW3rJzzi2tUDieF
9cYnJ112Rd+zwQT3mqdD5m7SnBQy4xN5wRYZ/tcdNc3kQJPS3q/xykojEzUDSOEQ
XrqWjNtuK1n8SXwvWa7wq8h6sC5X801xluCzi0UcxyhKKCkAOd9D
-----END CERTIFICATE-----
)";

constexpr char kSampleProp[] = R"(####################################
# from generate-common-build-props
# These properties identify this partition image.
####################################
ro.product.product.brand=google
ro.product.product.device=brya_cheets
ro.product.product.manufacturer=Google
ro.product.product.model=brya
ro.product.product.name=brya
ro.product.build.date=Fri Jun 28 16:27:22 UTC 2024
ro.product.build.date.utc=1719592042
#ro.product.build.id?=TP1A.220624.014
#ro.product.build.tags?=dev-keys
ro.product.build.type=userdebug
ro.product.build.version.incremental=12029833
ro.product.build.version.release=13
ro.product.build.version.release_or_codename=13
ro.product.build.version.sdk=33
)";

constexpr char kSampleSerialNumber[] = "9ADQWDE4R5OLHVSFG23NV34";

constexpr const char kProductBuildPropertyFileName[] = "product_build.prop";

const base::flat_map<std::string, std::string> kSampleDeviceIdMap = {
    {"brand", "google"},
    {"device", "brya_cheets"},
    {"manufacturer", "Google"},
    {"model", "brya"},
    {"product", "brya"}};
}  // namespace

class ArcRemoteProvisioningContextTest : public ::testing::Test {
 protected:
  ArcRemoteProvisioningContextTest() {}

  void SetUp() override {
    remote_provisioning_context_ =
        new ArcRemoteProvisioningContext(KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT);
  }

  void ExpectProvisionSuccess() {
    brillo::Blob dkCert = brillo::BlobFromString(kDKCertPEM);
    std::vector<brillo::Blob> kCertsOut{dkCert};

    EXPECT_CALL(*manager_, GetDkCertChain(testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<0>(kCertsOut),
            testing::Return(arc_attestation::AndroidStatus::ok())));
  }

  void ExpectSignSuccess(std::vector<uint8_t>& der_signature) {
    EXPECT_CALL(*manager_, SignWithP256Dk(testing::_, testing::_))
        .WillOnce(testing::DoAll(
            testing::SetArgReferee<1>(der_signature),
            testing::Return(arc_attestation::AndroidStatus::ok())));
  }

  void SetupManagerForTesting() {
    arc_attestation::ArcAttestationManagerSingleton::DestroyForTesting();
    arc_attestation::ArcAttestationManagerSingleton::CreateForTesting();
    std::unique_ptr<NiceMock<arc_attestation::MockArcAttestationManager>>
        manager = std::make_unique<
            NiceMock<arc_attestation::MockArcAttestationManager>>();
    manager_ = manager.get();
    arc_attestation::ArcAttestationManagerSingleton::Get()
        ->SetManagerForTesting(std::move(manager));
  }

  void TearDown() override {
    arc_attestation::ArcAttestationManagerSingleton::DestroyForTesting();
  }

  NiceMock<arc_attestation::MockArcAttestationManager>* manager_;
  ArcRemoteProvisioningContext* remote_provisioning_context_;
};

class ArcRemoteProvisioningContextTestPeer {
 public:
  void set_property_dir_for_tests(
      ArcRemoteProvisioningContext* remote_provisioning_context_,
      base::FilePath file_path) {
    remote_provisioning_context_->set_property_dir_for_tests(file_path);
  }

  void set_device_id_map_for_tests(
      ArcRemoteProvisioningContext* remote_provisioning_context_,
      const base::flat_map<std::string, std::string>& device_id_map) {
    remote_provisioning_context_->set_device_id_map_for_tests(device_id_map);
  }

  void set_serial_number_for_tests(
      ArcRemoteProvisioningContext* remote_provisioning_context_,
      const std::string& serial_number) {
    remote_provisioning_context_->set_serial_number_for_tests(serial_number);
  }
};

TEST_F(ArcRemoteProvisioningContextTest,
       createCoseSign1SignatureFromDKFailure) {
  std::vector<uint8_t> protectedParams = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload = {0x04, 0x05, 0x06};
  std::vector<uint8_t> aad = {};

  cppcose::ErrMsgOr<std::vector<uint8_t>> signature =
      createCoseSign1SignatureFromDK(protectedParams, payload, aad);

  // This should fail as we have not setup the Mock Arc Attestation manager yet.
  EXPECT_FALSE(signature);
}

TEST_F(ArcRemoteProvisioningContextTest, constructCoseSign1FromDKFailure) {
  std::vector<uint8_t> protectedParams = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload = {0x04, 0x05, 0x06};
  std::vector<uint8_t> aad = {};

  auto coseSign1 = constructCoseSign1FromDK(cppbor::Map(), payload, aad);

  // This should fail as we have not setup the Mock Arc Attestation manager yet.
  EXPECT_FALSE(coseSign1);
}

TEST_F(ArcRemoteProvisioningContextTest,
       createCoseSign1SignatureFromDKSuccess) {
  std::vector<uint8_t> protectedParams = {0x01, 0x02, 0x03};
  std::vector<uint8_t> payload = {0x04, 0x05, 0x06};
  std::vector<uint8_t> aad = {};

  // Prepare.
  SetupManagerForTesting();
  std::string bytes_string = absl::HexStringToBytes(kEcdsaDERSignatureHex);
  std::vector<uint8_t> byte_signature = brillo::BlobFromString(bytes_string);
  ExpectSignSuccess(byte_signature);

  // Execute.
  cppcose::ErrMsgOr<std::vector<uint8_t>> signature =
      createCoseSign1SignatureFromDK(protectedParams, payload, aad);

  // Test.
  ASSERT_TRUE(signature);
  EXPECT_EQ(signature.moveValue().size(), kP256SignatureLength);
}

TEST_F(ArcRemoteProvisioningContextTest, GenerateBccProductionMode) {
  // Prepare.
  SetupManagerForTesting();
  ExpectProvisionSuccess();
  std::string bytes_string = absl::HexStringToBytes(kEcdsaDERSignatureHex);
  std::vector<uint8_t> byte_signature = brillo::BlobFromString(bytes_string);
  ExpectSignSuccess(byte_signature);

  // Execute.
  auto result = remote_provisioning_context_->GenerateBcc(false);
  ASSERT_TRUE(result.has_value());
  auto bcc = std::move(result->second);

  // Test.
  ASSERT_TRUE(bcc.isCompound());
  auto coseKey = std::move(bcc.get(0));
  auto coseSign1 = std::move(bcc.get(1));
  const cppbor::Array* cose_sign = coseSign1->asArray();
  std::vector<uint8_t> additional_authenticated_data;
  EXPECT_TRUE(cppcose::verifyAndParseCoseSign1(cose_sign, coseKey->encode(),
                                               additional_authenticated_data));
}

TEST_F(ArcRemoteProvisioningContextTest, GenerateBccTestMode) {
  // Execute.
  auto result = remote_provisioning_context_->GenerateBcc(true);
  ASSERT_TRUE(result.has_value());
  auto bcc = std::move(result->second);
  // Test.
  ASSERT_TRUE(bcc.isCompound());
  auto coseKey = std::move(bcc.get(0));
  auto coseSign1 = std::move(bcc.get(1));
  const cppbor::Array* cose_sign = coseSign1->asArray();
  std::vector<uint8_t> additional_authenticated_data;
  EXPECT_TRUE(cppcose::verifyAndParseCoseSign1(cose_sign, coseKey->encode(),
                                               additional_authenticated_data));
}

TEST_F(ArcRemoteProvisioningContextTest,
       BuildProtectedDataPayloadProductionMode) {
  // Prepare.
  std::vector<uint8_t> additional_authenticated_data;
  std::vector<uint8_t> mac_key;
  SetupManagerForTesting();
  ExpectProvisionSuccess();
  std::string bytes_string = absl::HexStringToBytes(kEcdsaDERSignatureHex);
  std::vector<uint8_t> byte_signature = brillo::BlobFromString(bytes_string);
  brillo::Blob challenge = brillo::BlobFromString("I am a fake challenge");
  brillo::Blob random_blob = brillo::BlobFromString("I am a random blob");
  EXPECT_CALL(*manager_, QuoteCrOSBlob(challenge, testing::_))
      .Times(1)
      .WillRepeatedly(testing::DoAll(
          testing::SetArgReferee<1>(random_blob),
          testing::Return(arc_attestation::AndroidStatus::ok())));
  // We need signing twice here.
  // First time for Generate Bcc.
  // Second time for BuildProtectedDataPayload.
  EXPECT_CALL(*manager_, SignWithP256Dk(testing::_, testing::_))
      .Times(2)
      .WillRepeatedly(testing::DoAll(
          testing::SetArgReferee<1>(byte_signature),
          testing::Return(arc_attestation::AndroidStatus::ok())));
  remote_provisioning_context_->SetChallengeForCertificateRequest(challenge);

  // Execute.
  auto result = remote_provisioning_context_->BuildProtectedDataPayload(
      false, mac_key, additional_authenticated_data);

  // Test.
  EXPECT_TRUE(result);
}

TEST_F(ArcRemoteProvisioningContextTest, BuildProtectedDataPayloadTestMode) {
  // Prepare.
  std::vector<uint8_t> additional_authenticated_data;
  std::vector<uint8_t> mac_key;

  // Execute.
  auto result = remote_provisioning_context_->BuildProtectedDataPayload(
      true, mac_key, additional_authenticated_data);

  // Test.
  EXPECT_TRUE(result);
}

TEST_F(ArcRemoteProvisioningContextTest, ConvertDeviceIdMapSuccess) {
  // Execute.
  auto result = ConvertDeviceIdMap(kSampleDeviceIdMap);

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  EXPECT_EQ(result_map->size(), 5);

  ASSERT_TRUE(result_map->get("brand"));
  EXPECT_EQ(*result_map->get("brand"), cppbor::Tstr("google"));
  ASSERT_TRUE(result_map->get("device"));
  EXPECT_EQ(*result_map->get("device"), cppbor::Tstr("brya_cheets"));
  ASSERT_TRUE(result_map->get("manufacturer"));
  EXPECT_EQ(*result_map->get("manufacturer"), cppbor::Tstr("Google"));
  ASSERT_TRUE(result_map->get("model"));
  EXPECT_EQ(*result_map->get("model"), cppbor::Tstr("brya"));
  ASSERT_TRUE(result_map->get("product"));
  EXPECT_EQ(*result_map->get("product"), cppbor::Tstr("brya"));
}

TEST_F(ArcRemoteProvisioningContextTest, ConvertDeviceIdMapEmpty) {
  // Prepare.
  base::flat_map<std::string, std::string> empty_map;

  // Execute.
  auto result = ConvertDeviceIdMap(empty_map);

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  EXPECT_EQ(result_map->size(), 0);
}

TEST_F(ArcRemoteProvisioningContextTest, CreateDeviceIdMapSuccess) {
  // Prepare.
  std::string file_data(kSampleProp);
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      temp_dir.GetPath().Append(kProductBuildPropertyFileName), file_data));

  // Execute.
  auto result = CreateDeviceIdMap(temp_dir.GetPath());

  // Test.
  ASSERT_TRUE(result.has_value());

  auto result_map = result.value();
  EXPECT_EQ(result_map.size(), 5);
  ASSERT_EQ(result_map.count("brand"), 1);
  EXPECT_EQ(result_map.at("brand"), "google");
  ASSERT_EQ(result_map.count("device"), 1);
  EXPECT_EQ(result_map.at("device"), "brya_cheets");
  ASSERT_EQ(result_map.count("manufacturer"), 1);
  EXPECT_EQ(result_map.at("manufacturer"), "Google");
  ASSERT_EQ(result_map.count("model"), 1);
  EXPECT_EQ(result_map.at("model"), "brya");
  ASSERT_EQ(result_map.count("product"), 1);
  EXPECT_EQ(result_map.at("product"), "brya");
}

TEST_F(ArcRemoteProvisioningContextTest, CreateDeviceIdMapFailure) {
  // Prepare.
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  // Execute.
  std::optional<base::flat_map<std::string, std::string>> result =
      CreateDeviceIdMap(temp_dir.GetPath());

  // Test.
  ASSERT_FALSE(result.has_value());
}

TEST_F(ArcRemoteProvisioningContextTest, CreateDeviceInfoSuccess) {
  // Prepare.
  remote_provisioning_context_->SetSystemVersion(kOsVersion, kOsPatchLevel);
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);

  // Execute.
  auto result = remote_provisioning_context_->CreateDeviceInfo();

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  ASSERT_TRUE(result_map->get("brand"));
  EXPECT_EQ(*result_map->get("brand"), cppbor::Tstr("google"));
  ASSERT_TRUE(result_map->get("device"));
  EXPECT_EQ(*result_map->get("device"), cppbor::Tstr("brya_cheets"));
  ASSERT_TRUE(result_map->get("manufacturer"));
  EXPECT_EQ(*result_map->get("manufacturer"), cppbor::Tstr("Google"));
  ASSERT_TRUE(result_map->get("model"));
  EXPECT_EQ(*result_map->get("model"), cppbor::Tstr("brya"));
  ASSERT_TRUE(result_map->get("product"));
  EXPECT_EQ(*result_map->get("product"), cppbor::Tstr("brya"));
  ASSERT_TRUE(result_map->get("security_level"));
  EXPECT_EQ(*result_map->get("security_level"), cppbor::Tstr("tee"));
  ASSERT_TRUE(result_map->get("os_version"));
  EXPECT_EQ(*result_map->get("os_version"),
            cppbor::Tstr(std::to_string(kOsVersion)));
  ASSERT_TRUE(result_map->get("system_patch_level"));
  EXPECT_EQ(*result_map->get("system_patch_level"),
            cppbor::Uint(kOsPatchLevel));
  ASSERT_TRUE(result_map->get("version"));
  EXPECT_EQ(*result_map->get("version"), cppbor::Uint(kDeviceInfoMapVersion));
  ASSERT_TRUE(result_map->get("fused"));
  EXPECT_EQ(*result_map->get("fused"), cppbor::Uint(kSecureBootEnforced));
}

TEST_F(ArcRemoteProvisioningContextTest, CreateDeviceInfoFailure) {
  // Prepare.
  base::flat_map<std::string, std::string> empty_map;
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         empty_map);

  // Execute.
  auto result = remote_provisioning_context_->CreateDeviceInfo();

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  EXPECT_EQ(result_map->size(), 0);
}

TEST_F(ArcRemoteProvisioningContextTest, CreateDeviceInfoWithVerifiedBootInfo) {
  // Prepare.
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);
  const std::string unlocked_bootloader_state = "unlocked";
  const std::string unverified_boot_state = "orange";
  const std::string vbmeta_digest_string =
      "ab76eece2ea8e2bea108d4dfd618bb6ab41096b291c6e83937637a941d87b303";
  const std::vector<uint8_t> vbmeta_digest =
      brillo::BlobFromString(vbmeta_digest_string);

  // Execute.
  remote_provisioning_context_->SetVerifiedBootInfo(
      unverified_boot_state, unlocked_bootloader_state, vbmeta_digest);
  auto result = remote_provisioning_context_->CreateDeviceInfo();

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  ASSERT_TRUE(result_map->get("bootloader_state"));
  EXPECT_EQ(*result_map->get("bootloader_state"),
            cppbor::Tstr(unlocked_bootloader_state));
  ASSERT_TRUE(result_map->get("vb_state"));
  EXPECT_EQ(*result_map->get("vb_state"), cppbor::Tstr(unverified_boot_state));
  ASSERT_TRUE(result_map->get("vbmeta_digest"));
  EXPECT_EQ(*result_map->get("vbmeta_digest"),
            cppbor::Bstr(vbmeta_digest_string));
}

TEST_F(ArcRemoteProvisioningContextTest,
       CreateDeviceInfoWithoutVerifiedBootInfo) {
  // Prepare.
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);

  // Execute.
  auto result = remote_provisioning_context_->CreateDeviceInfo();

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  ASSERT_FALSE(result_map->get("bootloader_state"));
  ASSERT_FALSE(result_map->get("vb_state"));
  ASSERT_FALSE(result_map->get("vbmeta_digest"));
}

TEST_F(ArcRemoteProvisioningContextTest,
       CreateDeviceInfoWithEmptyVbMetaDigest) {
  // Prepare.
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);

  // Execute.
  remote_provisioning_context_->SetVerifiedBootInfo(
      /* boot_state */ "orange",
      /* bootloader_state */ "unlocked",
      /* vbmeta_digest */ {});
  auto result = remote_provisioning_context_->CreateDeviceInfo();

  // Test.
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->type() == cppbor::MAP);
  auto result_map = result->asMap();
  ASSERT_TRUE(result_map);
  ASSERT_FALSE(result_map->get("vbmeta_digest"));
}

TEST_F(ArcRemoteProvisioningContextTest, VerifyAndCopyDeviceIdsSuccess) {
  // Prepare.
  remote_provisioning_context_->SetSystemVersion(kOsVersion, kOsPatchLevel);
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);
  test_peer->set_serial_number_for_tests(remote_provisioning_context_,
                                         kSampleSerialNumber);
  ::keymaster::AuthorizationSet input_params;
  ::keymaster::AuthorizationSet output_params;

  auto brand_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("brand"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_BRAND,
                         brand_blob.data(), brand_blob.size());
  auto device_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("device"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_DEVICE,
                         device_blob.data(), device_blob.size());
  auto product_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("product"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_PRODUCT,
                         product_blob.data(), product_blob.size());
  auto manufacturer_blob =
      brillo::BlobFromString(kSampleDeviceIdMap.at("manufacturer"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MANUFACTURER,
                         manufacturer_blob.data(), manufacturer_blob.size());
  auto model_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("model"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MODEL,
                         model_blob.data(), model_blob.size());
  auto serial_blob = brillo::BlobFromString(kSampleSerialNumber);
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_SERIAL,
                         serial_blob.data(), serial_blob.size());

  // Execute.
  keymaster_error_t error =
      remote_provisioning_context_->VerifyAndCopyDeviceIds(input_params,
                                                           &output_params);

  // Test.
  EXPECT_EQ(error, KM_ERROR_OK);
  // Serial Number does not exist in the device id map.
  // Hence, it is added separately.
  EXPECT_EQ(output_params.size(), kSampleDeviceIdMap.size() + 1);
}

TEST_F(ArcRemoteProvisioningContextTest, VerifyAndCopyDeviceIdsMismatch) {
  // Prepare.
  remote_provisioning_context_->SetSystemVersion(kOsVersion, kOsPatchLevel);
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);
  ::keymaster::AuthorizationSet input_params;
  ::keymaster::AuthorizationSet output_params;

  auto brand_blob = brillo::BlobFromString("fake_brand");
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_BRAND,
                         brand_blob.data(), brand_blob.size());
  auto device_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("device"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_DEVICE,
                         device_blob.data(), device_blob.size());
  auto product_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("product"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_PRODUCT,
                         product_blob.data(), product_blob.size());
  auto manufacturer_blob =
      brillo::BlobFromString(kSampleDeviceIdMap.at("manufacturer"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MANUFACTURER,
                         manufacturer_blob.data(), manufacturer_blob.size());
  auto model_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("model"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MODEL,
                         model_blob.data(), model_blob.size());

  // Execute.
  keymaster_error_t error =
      remote_provisioning_context_->VerifyAndCopyDeviceIds(input_params,
                                                           &output_params);

  // Test.
  EXPECT_EQ(error, KM_ERROR_CANNOT_ATTEST_IDS);
  EXPECT_EQ(output_params.size(), 0);
}

TEST_F(ArcRemoteProvisioningContextTest,
       VerifyAndCopyDeviceIdsEmptySerialFailure) {
  // Prepare.
  remote_provisioning_context_->SetSystemVersion(kOsVersion, kOsPatchLevel);
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);
  ::keymaster::AuthorizationSet input_params;
  ::keymaster::AuthorizationSet output_params;

  auto brand_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("brand"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_BRAND,
                         brand_blob.data(), brand_blob.size());
  auto device_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("device"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_DEVICE,
                         device_blob.data(), device_blob.size());
  auto product_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("product"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_PRODUCT,
                         product_blob.data(), product_blob.size());
  auto manufacturer_blob =
      brillo::BlobFromString(kSampleDeviceIdMap.at("manufacturer"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MANUFACTURER,
                         manufacturer_blob.data(), manufacturer_blob.size());
  auto model_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("model"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MODEL,
                         model_blob.data(), model_blob.size());
  auto serial_blob = brillo::BlobFromString(kSampleSerialNumber);
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_SERIAL,
                         serial_blob.data(), serial_blob.size());

  // Execute.
  keymaster_error_t error =
      remote_provisioning_context_->VerifyAndCopyDeviceIds(input_params,
                                                           &output_params);

  // Test.
  EXPECT_EQ(error, KM_ERROR_CANNOT_ATTEST_IDS);
  EXPECT_TRUE(output_params.empty());
}

TEST_F(ArcRemoteProvisioningContextTest,
       VerifyAndCopyDeviceIdsMismatchSerialFailure) {
  // Prepare.
  remote_provisioning_context_->SetSystemVersion(kOsVersion, kOsPatchLevel);
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  test_peer->set_device_id_map_for_tests(remote_provisioning_context_,
                                         kSampleDeviceIdMap);
  test_peer->set_serial_number_for_tests(remote_provisioning_context_,
                                         kSampleSerialNumber);
  ::keymaster::AuthorizationSet input_params;
  ::keymaster::AuthorizationSet output_params;

  auto brand_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("brand"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_BRAND,
                         brand_blob.data(), brand_blob.size());
  auto device_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("device"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_DEVICE,
                         device_blob.data(), device_blob.size());
  auto product_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("product"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_PRODUCT,
                         product_blob.data(), product_blob.size());
  auto manufacturer_blob =
      brillo::BlobFromString(kSampleDeviceIdMap.at("manufacturer"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MANUFACTURER,
                         manufacturer_blob.data(), manufacturer_blob.size());
  auto model_blob = brillo::BlobFromString(kSampleDeviceIdMap.at("model"));
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_MODEL,
                         model_blob.data(), model_blob.size());
  auto serial_blob = brillo::BlobFromString("I am a fake serial");
  input_params.push_back(::keymaster::TAG_ATTESTATION_ID_SERIAL,
                         serial_blob.data(), serial_blob.size());

  // Execute.
  keymaster_error_t error =
      remote_provisioning_context_->VerifyAndCopyDeviceIds(input_params,
                                                           &output_params);

  // Test.
  EXPECT_EQ(error, KM_ERROR_CANNOT_ATTEST_IDS);
  EXPECT_TRUE(output_params.empty());
}

TEST_F(ArcRemoteProvisioningContextTest, VerifyAndCopyDeviceIdsEmpty) {
  // Prepare.
  remote_provisioning_context_->SetSystemVersion(kOsVersion, kOsPatchLevel);
  auto test_peer = std::make_unique<ArcRemoteProvisioningContextTestPeer>();
  ::keymaster::AuthorizationSet input_params;
  ::keymaster::AuthorizationSet output_params;

  // Execute.
  keymaster_error_t error =
      remote_provisioning_context_->VerifyAndCopyDeviceIds(input_params,
                                                           &output_params);

  // Test.
  EXPECT_EQ(error, KM_ERROR_CANNOT_ATTEST_IDS);
  EXPECT_EQ(output_params.size(), 0);
}

TEST_F(ArcRemoteProvisioningContextTest, SetSerialNumber_Success) {
  // Prepare.
  std::string serial_number("4987fwehjn1271j231293fqdesb02vs912e");

  // Execute.
  keymaster_error_t error =
      remote_provisioning_context_->SetSerialNumber(serial_number);

  // Test.
  EXPECT_EQ(error, KM_ERROR_OK);
}

TEST_F(ArcRemoteProvisioningContextTest, SetSerialNumber_Failure) {
  // Execute.
  keymaster_error_t error = remote_provisioning_context_->SetSerialNumber("");

  // Test.
  EXPECT_EQ(error, KM_ERROR_UNKNOWN_ERROR);
}

TEST_F(ArcRemoteProvisioningContextTest, SetSerialNumberTwice_Failure) {
  // Prepare.
  std::string serial_number_1("4987fwehjn1271j231293fqdesb02vs912e");
  std::string serial_number_2("304730947georgnert9holhxlhsi713nvlb");

  // Execute.
  keymaster_error_t error_1 =
      remote_provisioning_context_->SetSerialNumber(serial_number_1);
  keymaster_error_t error_2 =
      remote_provisioning_context_->SetSerialNumber(serial_number_2);

  // Test.
  EXPECT_EQ(error_1, KM_ERROR_OK);
  EXPECT_EQ(error_2, KM_ERROR_UNKNOWN_ERROR);
}

}  // namespace arc::keymint::context
