// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

#include <algorithm>
#include <string>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <crypto/random.h>
#include <keymaster/cppcose/cppcose.h>
#include <libarc-attestation/lib/interface.h>
#include <openssl/rand.h>

/*
A lot of data structures in this file mimic the structures in
|ProtectedData.aidl| -
https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/ProtectedData.aidl.
*/
namespace arc::keymint::context {

constexpr uint32_t kP256AffinePointSize = 32;
constexpr uint32_t kP256SignatureLength = 64;
constexpr uint32_t kP256EcdsaPrivateKeyLength = 32;
constexpr uint32_t kSeedSize = 32;
// Key is decided in agreement with Android Remote Provisioning Team.
constexpr char kChromeOSQuotedBlobKey[] = "ChromeOS PCA ARC v1";
// CDDL Schema version.
/*
Device Info Map version is linked from here -
https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/security/rkp/aidl/android/hardware/security/keymint/DeviceInfoV2.cddl
*/
constexpr uint32_t kDeviceInfoMapVersion = 2;
constexpr uint32_t kSecureBootEnforced = 0;
const std::vector<uint8_t> kBccPayloadKeyUsage{0x20};
constexpr const char kProductBuildPropertyRootDir[] =
    "/usr/share/arcvm/properties/";
constexpr const char kProductBuildPropertyFileName[] = "product_build.prop";
constexpr char kProductBrand[] = "ro.product.product.brand";
constexpr char kProductDevice[] = "ro.product.product.device";
constexpr char kProductManufacturer[] = "ro.product.product.manufacturer";
constexpr char kProductModel[] = "ro.product.product.model";
constexpr char kProductName[] = "ro.product.product.name";

/*
This function creates BccEntryInput and then returns it after signing
by the key from CrOS DK cert.
*/
cppcose::ErrMsgOr<std::vector<uint8_t>> createCoseSign1SignatureFromDK(
    const std::vector<uint8_t>& protectedParams,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData) {
  // |signatureInput| is the BccEntryInput structure for |ProtectedData.aidl|.
  std::vector<uint8_t> signatureInput = cppbor::Array()
                                            .add("Signature1")
                                            .add(protectedParams)
                                            .add(additionalAuthData)
                                            .add(payload)
                                            .encode();

  std::vector<uint8_t> ecdsaDERSignature(kP256SignatureLength);
  arc_attestation::AndroidStatus status =
      arc_attestation::SignWithP256Dk(signatureInput, ecdsaDERSignature);

  if (!status.is_ok()) {
    LOG(ERROR) << "Signing by libarc-attestation failed";
    int32_t error_code = status.get_error_code();
    std::string error = "Error Message = " + status.get_message() +
                        ", Error Code = " + std::to_string(error_code);
    return error;
  }

  // The signature returned from libarc-attestation is in DER format.
  // Convert it to COSE Format.
  cppcose::ErrMsgOr<std::vector<uint8_t>> p256DkSignature =
      cppcose::ecdsaDerSignatureToCose(ecdsaDERSignature);

  if (!p256DkSignature) {
    auto errorMessage = p256DkSignature.moveMessage();
    LOG(ERROR) << "Error extracting Cose Signature from Chrome OS ECDSA Der "
                  "Signature: "
               << errorMessage;
    return errorMessage;
  }
  return p256DkSignature;
}

/*
This function returns BccEntry as in |ProtectedData.aidl|
*/
cppcose::ErrMsgOr<cppbor::Array> constructCoseSign1FromDK(
    cppbor::Map protectedParamsMap,
    const std::vector<uint8_t>& payload,
    const std::vector<uint8_t>& additionalAuthData) {
  std::vector<uint8_t> protectedParams =
      protectedParamsMap.add(cppcose::ALGORITHM, cppcose::ES256)
          .canonicalize()
          .encode();

  // |signature| represents BccEntryInput from |ProtectedtData.aidl|.
  auto signature = createCoseSign1SignatureFromDK(protectedParams, payload,
                                                  additionalAuthData);
  if (!signature) {
    return signature.moveMessage();
  }

  // Unprotected Parameters.
  auto unprotectedParams = cppbor::Map();

  // Returns the Bcc Entry.
  return cppbor::Array()
      .add(std::move(protectedParams))
      .add(std::move(unprotectedParams))
      .add(std::move(payload))
      .add(signature.moveValue());
}

std::optional<base::flat_map<std::string, std::string>> CreateDeviceIdMap(
    const base::FilePath& property_dir) {
  base::flat_map<std::string, std::string> device_id_map;
  const base::FilePath prop_file_path =
      property_dir.Append(kProductBuildPropertyFileName);
  std::string properties_content;
  if (!base::ReadFileToString(prop_file_path, &properties_content)) {
    // In case of failure to read properties into string, return nullopt.
    LOG(ERROR) << "Failed to create device ID map because of failure to read "
                  "properties from the properties file";
    return std::nullopt;
  }

  std::vector<std::string> properties = base::SplitString(
      properties_content, "\n", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);

  base::flat_map<std::string, std::pair<std::string, std::string>>
      property_map = {
          {kProductBrand, std::make_pair("brand", "")},
          {kProductDevice, std::make_pair("device", "")},
          {kProductManufacturer, std::make_pair("manufacturer", "")},
          {kProductModel, std::make_pair("model", "")},
          {kProductName, std::make_pair("product", "")}};

  constexpr char separator[] = "=";

  // If the property exists in input properties, add its value in property map.
  for (const auto& property : properties) {
    auto separatorIndex = property.find(separator);
    auto key = property.substr(0, separatorIndex);

    auto itr = property_map.find(key);
    if (itr != property_map.end()) {
      auto value = property.substr(separatorIndex + 1, property.size());
      itr->second.second = value;
      device_id_map.insert({itr->second.first, value});
    }
  }

  return device_id_map;
}

std::unique_ptr<cppbor::Map> ConvertDeviceIdMap(
    const base::flat_map<std::string, std::string>& device_id_map) {
  auto result = std::make_unique<cppbor::Map>(cppbor::Map());
  // Convert property map into cppbor map.
  for (auto& [key, value] : device_id_map) {
    result->add(cppbor::Tstr(key), cppbor::Tstr(value));
  }
  return result;
}

namespace {

std::optional<std::vector<uint8_t>> provisionAndFetchDkCert() {
  // Provision certificate.
  arc_attestation::AndroidStatus provision_status =
      arc_attestation::ProvisionDkCert(true /*blocking*/);
  if (!provision_status.is_ok()) {
    LOG(ERROR) << "Error in Provisioning Dk Cert from libarc-attestation";
    return std::nullopt;
  }

  // Extract DK Cert Chain from libarc-attestation.
  std::vector<std::vector<uint8_t>> cert_chain;
  arc_attestation::AndroidStatus cert_status =
      arc_attestation::GetDkCertChain(cert_chain);
  if (!cert_status.is_ok()) {
    LOG(ERROR) << "Error in fetching DK Cert Chain from libarc-attestation";
    return std::nullopt;
  }

  if (cert_chain.size() == 0) {
    LOG(ERROR) << "DK Cert Chain from libarc-attestation is empty";
    return std::nullopt;
  }
  // First element of cert chain carries UDS Pub.
  return cert_chain.front();
}

// Generates Boot Certificate Chain for Test mode.
// |private_key_vector| is passed as a parameter, which is filled with the
// actual private key from this function.
cppcose::ErrMsgOr<cppbor::Array> GenerateBccForTestMode(
    bool test_mode, std::vector<uint8_t>& private_key_vector) {
  if (!test_mode) {
    auto error_message = "Not Allowed to generate Test BCC in Production Mode";
    LOG(ERROR) << error_message;
    return error_message;
  }

  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);
  absl::Span<uint8_t> private_key(private_key_vector);

  // Get ECDSA key from Seed in Test Mode.
  std::vector<uint8_t> seed_vector = crypto::RandBytesAsVector(kSeedSize);
  absl::Span<uint8_t> seed(seed_vector);
  std::string private_key_pem;
  auto error = GenerateEcdsa256KeyFromSeed(test_mode, seed, private_key,
                                           private_key_pem, x_coord, y_coord);
  if (error != KM_ERROR_OK) {
    auto error_message = "Failed to get ECDSA key from seed in test mode";
    LOG(ERROR) << error_message;
    return error_message;
  }

  auto coseKey =
      cppbor::Map()
          .add(cppcose::CoseKey::KEY_TYPE, cppcose::EC2)
          .add(cppcose::CoseKey::ALGORITHM, cppcose::ES256)
          .add(cppcose::CoseKey::CURVE, cppcose::P256)
          .add(cppcose::CoseKey::KEY_OPS, cppbor::Array().add(cppcose::VERIFY))
          .add(cppcose::CoseKey::PUBKEY_X, x_vect)
          .add(cppcose::CoseKey::PUBKEY_Y, y_vect)
          .canonicalize();

  // This map is based on the Protected Data AIDL, which is further based on
  // the Open Profile for DICE.
  // |sign1Payload| represents BccPayload data structure from
  // |ProtectedData.aidl|. Fields - Issuer and Subject are redundant for a
  // degenerate Bcc chain like here.
  auto sign1Payload =
      cppbor::Map()
          .add(BccPayloadLabel::ISSUER, "Issuer")
          .add(BccPayloadLabel::SUBJECT, "Subject")
          .add(BccPayloadLabel::SUBJECT_PUBLIC_KEY, coseKey.encode())
          .add(BccPayloadLabel::KEY_USAGE, kBccPayloadKeyUsage)
          .canonicalize()
          .encode();
  std::vector<uint8_t> additional_authenticated_data;

  cppcose::ErrMsgOr<cppbor::Array> coseSign1("");
  coseSign1 = cppcose::constructECDSACoseSign1(private_key_vector,
                                               cppbor::Map(), sign1Payload,
                                               additional_authenticated_data);
  if (!coseSign1) {
    auto error_message = coseSign1.moveMessage();
    LOG(ERROR) << "Bcc Generation failed in test mode: " << error_message;
    return error_message;
  }
  auto cbor_array =
      cppbor::Array().add(std::move(coseKey)).add(coseSign1.moveValue());
  return cbor_array;
}

// This function generates Boot Chain Certificate for Production mode.
// Final signature is signed by libarc-attestation.
cppcose::ErrMsgOr<cppbor::Array> GenerateBccForProductionMode() {
  std::vector<uint8_t> x_vect(kP256AffinePointSize);
  std::vector<uint8_t> y_vect(kP256AffinePointSize);
  absl::Span<uint8_t> x_coord(x_vect);
  absl::Span<uint8_t> y_coord(y_vect);

  std::optional<std::vector<uint8_t>> uds_pub = provisionAndFetchDkCert();
  if (!uds_pub.has_value()) {
    auto error_message =
        "Failed to get a valid device cert from libarc-attestation";
    return error_message;
  }

  // Extract Affine coordinates from libarc-attestation certificate.
  // Get ECDSA Key from Device Cert in Production Mode.
  auto error = GetEcdsa256KeyFromCertBlob(uds_pub.value(), x_coord, y_coord);
  if (error != KM_ERROR_OK) {
    auto error_message =
        "Failed to extract Affine coordinates from ChromeOS cert";
    LOG(ERROR) << error_message;
    return error_message;
  }

  // Construct Cose Key.
  auto coseKey =
      cppbor::Map()
          .add(cppcose::CoseKey::KEY_TYPE, cppcose::EC2)
          .add(cppcose::CoseKey::ALGORITHM, cppcose::ES256)
          .add(cppcose::CoseKey::CURVE, cppcose::P256)
          .add(cppcose::CoseKey::KEY_OPS, cppbor::Array().add(cppcose::VERIFY))
          .add(cppcose::CoseKey::PUBKEY_X, x_vect)
          .add(cppcose::CoseKey::PUBKEY_Y, y_vect)
          .canonicalize();

  // This map is based on the Protected Data AIDL, which is further based on
  // the Open Profile for DICE.
  // |sign1Payload| represents BccPayload data structure from
  // |ProtectedData.aidl|. Fields - Issuer and Subject are redundant for a
  // degenerate Bcc chain like here.
  auto sign1Payload =
      cppbor::Map()
          .add(BccPayloadLabel::ISSUER, "Issuer")
          .add(BccPayloadLabel::SUBJECT, "Subject")
          .add(BccPayloadLabel::SUBJECT_PUBLIC_KEY, coseKey.encode())
          .add(BccPayloadLabel::KEY_USAGE, kBccPayloadKeyUsage)
          .canonicalize()
          .encode();
  std::vector<uint8_t> additional_authenticated_data;

  // |coseSign1| represents the Bcc entry.
  auto coseSign1 = constructCoseSign1FromDK(cppbor::Map(), sign1Payload,
                                            additional_authenticated_data);
  if (!coseSign1) {
    auto error_message = coseSign1.moveMessage();
    LOG(ERROR) << "Bcc Generation failed in Production Mode: " << error_message;
    return error_message;
  }
  auto cbor_array =
      cppbor::Array().add(std::move(coseKey)).add(coseSign1.moveValue());
  return cbor_array;
}

// Return true if entries match, false otherwise.
bool matchAttestationId(keymaster_blob_t blob, const std::string& id) {
  if (blob.data_length != id.size()) {
    return false;
  }

  if (memcmp(blob.data, id.data(), id.size())) {
    return false;
  }
  return true;
}

}  // namespace

ArcRemoteProvisioningContext::ArcRemoteProvisioningContext(
    keymaster_security_level_t security_level)
    : PureSoftRemoteProvisioningContext(security_level),
      security_level_(security_level),
      property_dir_(kProductBuildPropertyRootDir) {
  device_id_map_ = CreateDeviceIdMap(property_dir_);
}

ArcRemoteProvisioningContext::~ArcRemoteProvisioningContext() = default;

std::optional<std::pair<std::vector<uint8_t>, cppbor::Array>>
ArcRemoteProvisioningContext::GenerateBcc(bool test_mode) const {
  std::vector<uint8_t> private_key_vector(kP256EcdsaPrivateKeyLength);

  cppcose::ErrMsgOr<cppbor::Array> cbor_array("");
  if (test_mode) {
    // Test Mode.
    cbor_array = GenerateBccForTestMode(test_mode, private_key_vector);
  } else {
    // Production Mode.
    cbor_array = GenerateBccForProductionMode();
  }

  if (!cbor_array) {
    auto error_message = cbor_array.moveMessage();
    LOG(ERROR) << "Bcc Generation failed: " << error_message;
    return std::nullopt;
  }

  // Boot Certificate Chain.
  return std::make_pair(std::move(private_key_vector), cbor_array.moveValue());
}

cppcose::ErrMsgOr<std::vector<uint8_t>>
ArcRemoteProvisioningContext::BuildProtectedDataPayload(
    bool test_mode,
    const std::vector<uint8_t>& mac_key,
    const std::vector<uint8_t>& additional_auth_data) const {
  cppbor::Array boot_cert_chain;
  cppcose::ErrMsgOr<cppbor::Array> signed_mac("");
  std::optional<cppbor::Map> cros_blob_map;
  if (test_mode) {
    // In Test mode, signature is constructed by signing with the
    // seed generated Ecdsa key.
    auto bcc = GenerateBcc(/*test_mode*/ true);
    std::vector<uint8_t> signing_key_test_mode;
    if (bcc.has_value()) {
      // Extract signing key and boot cert chain from the pair
      // returned by GenerateBcc function.
      signing_key_test_mode = std::move(bcc.value().first);
      boot_cert_chain = std::move(bcc.value().second);
      signed_mac = cppcose::constructECDSACoseSign1(
          signing_key_test_mode, cppbor::Map(), mac_key, additional_auth_data);
    }
  } else {
    // In Production mode, libarc-attestation does the signing.
    ArcLazyInitProdBcc();
    auto clone = boot_cert_chain_.clone();
    if (clone == nullptr || clone->type() != cppbor::ARRAY) {
      auto error_message = "The Boot Cert Chain is not an array";
      LOG(ERROR) << error_message;
      return error_message;
    }
    boot_cert_chain = std::move(*clone->asArray());

    if (!certificate_challenge_.has_value()) {
      auto error_message =
          "Challenge required for getting ChromeOS blob is not set";
      return error_message;
    }
    std::vector<uint8_t> cros_quoted_blob;
    const std::vector<uint8_t> certificate_challenge(
        certificate_challenge_.value().begin(),
        certificate_challenge_.value().end());
    arc_attestation::AndroidStatus blob_status =
        arc_attestation::QuoteCrOSBlob(certificate_challenge, cros_quoted_blob);
    if (!blob_status.is_ok() || cros_quoted_blob.empty()) {
      auto error_message =
          " Failed to get ChromeOS quoted blob from libarc-attestation";
      return error_message;
    }
    // Create a Cppbor Map with pre-defined key and value as Chrome OS blob
    // returned from libarc-attestation.
    cros_blob_map = cppbor::Map().add(cppbor::Tstr(kChromeOSQuotedBlobKey),
                                      cppbor::Array().add(cros_quoted_blob));

    signed_mac = constructCoseSign1FromDK(/*Protected Params*/ {}, mac_key,
                                          additional_auth_data);
  }

  if (!signed_mac) {
    auto error_message = signed_mac.moveMessage();
    LOG(ERROR) << "Signing while building Protected Data Payload failed: "
               << error_message;
    return error_message;
  }

  if (boot_cert_chain.size() == 0) {
    std::string error_message =
        "Boot Cert Chain is empty while building protected data payload";
    LOG(ERROR) << "ARC Remote Provisioning Context: " << error_message;
    return error_message;
  }

  cppbor::Array result = cppbor::Array()
                             .add(signed_mac.moveValue())
                             .add(std::move(boot_cert_chain));

  if (cros_blob_map.has_value()) {
    result.add(std::move(cros_blob_map.value().canonicalize()));
  }

  return result.encode();
}

void ArcRemoteProvisioningContext::ArcLazyInitProdBcc() const {
  std::call_once(bcc_initialized_flag_, [this]() {
    auto bcc = GenerateBcc(/*test_mode=*/false);
    if (bcc.has_value()) {
      // Extract boot cert chain from the pair returned by GenerateBcc.
      // In Production mode, the first element of the pair - |private key|
      // is not used.
      boot_cert_chain_ = std::move(bcc.value().second);
    }
  });
}

void ArcRemoteProvisioningContext::set_property_dir_for_tests(
    base::FilePath& path) {
  property_dir_ = base::FilePath(path);
}

void ArcRemoteProvisioningContext::set_device_id_map_for_tests(
    const base::flat_map<std::string, std::string>& device_id_map) {
  device_id_map_ = device_id_map;
}

void ArcRemoteProvisioningContext::set_serial_number_for_tests(
    const std::string& serial_number) {
  serial_number_ = serial_number;
}

void ArcRemoteProvisioningContext::SetSystemVersion(uint32_t os_version,
                                                    uint32_t os_patchlevel) {
  os_version_ = os_version;
  os_patchlevel_ = os_patchlevel;
}

void ArcRemoteProvisioningContext::SetVerifiedBootInfo(
    std::string_view boot_state,
    std::string_view bootloader_state,
    const std::vector<uint8_t>& vbmeta_digest) {
  verified_boot_state_ = boot_state;
  bootloader_state_ = bootloader_state;
  if (!vbmeta_digest.empty()) {
    vbmeta_digest_ = vbmeta_digest;
  }
}

void ArcRemoteProvisioningContext::SetChallengeForCertificateRequest(
    std::vector<uint8_t>& challenge) {
  certificate_challenge_ = std::make_optional<std::vector<uint8_t>>(
      challenge.begin(), challenge.end());
}

std::unique_ptr<cppbor::Map> ArcRemoteProvisioningContext::CreateDeviceInfo()
    const {
  if (!device_id_map_.has_value() || device_id_map_.value().empty()) {
    // In case of empty device id map, return a blank map.
    LOG(ERROR) << "Failed to return values for CreateDeviceInfo because device "
                  "ID map is empty";
    return std::make_unique<cppbor::Map>(cppbor::Map());
  }

  auto device_info_map = ConvertDeviceIdMap(device_id_map_.value());

  if (bootloader_state_.has_value()) {
    device_info_map->add(cppbor::Tstr("bootloader_state"),
                         cppbor::Tstr(bootloader_state_.value()));
  }
  if (verified_boot_state_.has_value()) {
    device_info_map->add(cppbor::Tstr("vb_state"),
                         cppbor::Tstr(verified_boot_state_.value()));
  }
  if (vbmeta_digest_.has_value()) {
    device_info_map->add(cppbor::Tstr("vbmeta_digest"),
                         cppbor::Bstr(vbmeta_digest_.value()));
  }
  if (os_version_.has_value()) {
    device_info_map->add(cppbor::Tstr("os_version"),
                         cppbor::Tstr(std::to_string(os_version_.value())));
  }

  if (os_patchlevel_.has_value()) {
    device_info_map->add(cppbor::Tstr("system_patch_level"),
                         cppbor::Uint(os_patchlevel_.value()));
  }
  if (vendor_patchlevel_.has_value()) {
    device_info_map->add(cppbor::Tstr("vendor_patch_level"),
                         cppbor::Uint(vendor_patchlevel_.value()));
  }
  device_info_map->add(cppbor::Tstr("version"),
                       cppbor::Uint(kDeviceInfoMapVersion));
  device_info_map->add(cppbor::Tstr("fused"),
                       cppbor::Uint(kSecureBootEnforced));

  const char* security_level = "tee";
  if (security_level_ == KM_SECURITY_LEVEL_TRUSTED_ENVIRONMENT) {
    device_info_map->add(cppbor::Tstr("security_level"),
                         cppbor::Tstr(security_level));
  }

  device_info_map->canonicalize();
  return device_info_map;
}

keymaster_error_t ArcRemoteProvisioningContext::SetSerialNumber(
    const std::string& serial_number) {
  if (serial_number.empty()) {
    LOG(ERROR) << "Cannot set empty serial number in KeyMint.";
    return KM_ERROR_UNKNOWN_ERROR;
  }

  if (serial_number_.has_value()) {
    LOG(ERROR) << "Cannot set serial number more than once in KeyMint.";
    return KM_ERROR_UNKNOWN_ERROR;
  }
  serial_number_ = serial_number;
  return KM_ERROR_OK;
}

void ArcRemoteProvisioningContext::SetVendorPatchlevel(
    uint32_t vendor_patchlevel) {
  vendor_patchlevel_ = vendor_patchlevel;
}

void ArcRemoteProvisioningContext::SetBootPatchlevel(uint32_t boot_patchlevel) {
  boot_patchlevel_ = boot_patchlevel;
}

keymaster_error_t ArcRemoteProvisioningContext::VerifyAndCopyDeviceIds(
    const ::keymaster::AuthorizationSet& attestation_params,
    ::keymaster::AuthorizationSet* attestation) const {
  if (!device_id_map_.has_value()) {
    return KM_ERROR_CANNOT_ATTEST_IDS;
  }

  auto device_id_map = device_id_map_.value();
  for (auto& entry : attestation_params) {
    bool found_mismatch = false;
    switch (entry.tag) {
      case KM_TAG_ATTESTATION_ID_BRAND:
        found_mismatch |=
            !matchAttestationId(entry.blob, device_id_map["brand"]);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_DEVICE:
        found_mismatch |=
            !matchAttestationId(entry.blob, device_id_map["device"]);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_PRODUCT:
        found_mismatch |=
            !matchAttestationId(entry.blob, device_id_map["product"]);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_MANUFACTURER:
        found_mismatch |=
            !matchAttestationId(entry.blob, device_id_map["manufacturer"]);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_MODEL:
        found_mismatch |=
            !matchAttestationId(entry.blob, device_id_map["model"]);
        attestation->push_back(entry);
        break;

      case KM_TAG_ATTESTATION_ID_IMEI:
      case KM_TAG_ATTESTATION_ID_MEID:
        found_mismatch = true;
        break;

      case KM_TAG_ATTESTATION_ID_SERIAL:
        if (!serial_number_.has_value()) {
          found_mismatch = true;
          break;
        }
        found_mismatch =
            !matchAttestationId(entry.blob, serial_number_.value());
        attestation->push_back(entry);
        break;

      default:
        // Ignore non-ID tags.
        break;
    }

    if (found_mismatch) {
      attestation->Clear();
      return KM_ERROR_CANNOT_ATTEST_IDS;
    }
  }

  return KM_ERROR_OK;
}

}  // namespace arc::keymint::context
