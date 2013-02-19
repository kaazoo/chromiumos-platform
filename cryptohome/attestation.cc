// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "attestation.h"

#include <algorithm>
#include <string>

#include <arpa/inet.h>
#include <base/stl_util.h>
#include <base/time.h>
#include <chromeos/secure_blob.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include "attestation.pb.h"
#include "cryptolib.h"
#include "keystore.h"
#include "pkcs11_keystore.h"
#include "platform.h"
#include "tpm.h"

using chromeos::SecureBlob;
using std::string;

namespace cryptohome {

const size_t Attestation::kQuoteExternalDataSize = 20;
const size_t Attestation::kCipherKeySize = 32;
const size_t Attestation::kCipherBlockSize = 16;
const size_t Attestation::kNonceSize = 20;  // As per TPM_NONCE definition.
const size_t Attestation::kDigestSize = 20;  // As per TPM_DIGEST definition.
const char* Attestation::kDefaultDatabasePath =
    "/mnt/stateful_partition/unencrypted/preserve/attestation.epb";

// This has been extracted from the Chrome OS PCA's encryption certificate.
const char* Attestation::kDefaultPCAPublicKey =
    "A2976637E113CC457013F4334312A416395B08D4B2A9724FC9BAD65D0290F39C"
    "866D1163C2CD6474A24A55403C968CF78FA153C338179407FE568C6E550949B1"
    "B3A80731BA9311EC16F8F66060A2C550914D252DB90B44D19BC6C15E923FFCFB"
    "E8A366038772803EE57C7D7E5B3D5E8090BF0960D4F6A6644CB9A456708508F0"
    "6C19245486C3A49F807AB07C65D5E9954F4F8832BC9F882E9EE1AAA2621B1F43"
    "4083FD98758745CBFFD6F55DA699B2EE983307C14C9990DDFB48897F26DF8FB2"
    "CFFF03E631E62FAE59CBF89525EDACD1F7BBE0BA478B5418E756FF3E14AC9970"
    "D334DB04A1DF267D2343C75E5D282A287060D345981ABDA0B2506AD882579FEF";

const Attestation::CertificateAuthority Attestation::kKnownEndorsementCA[] = {
  {"IFX TPM EK Intermediate CA 06",
   "de9e58a353313d21d683c687d6aaaab240248717557c077161c5e515f41d8efa"
   "48329f45658fb550f43f91d1ba0c2519429fb6ef964f89657098c90a9783ad6d"
   "3baea625db044734c478768db53b6022c556d8174ed744bd6e4455665715cd5c"
   "beb7c3fcb822ab3dfab1ecee1a628c3d53f6085983431598fb646f04347d5ae0"
   "021d5757cc6e3027c1e13f10633ae48bbf98732c079c17684b0db58bd0291add"
   "e277b037dd13fa3db910e81a4969622a79c85ac768d870f079b54c2b98c856e7"
   "15ef0ba9c01ee1da1241838a1307fe94b1ddfa65cdf7eeaa7e5b4b8a94c3dcd0"
   "29bb5ebcfc935e56641f4c8cb5e726c68f9dd6b41f8602ef6dc78d870a773571"},
  {"IFX TPM EK Intermediate CA 07",
   "f04c9b5b9f3cbc2509179f5e0f31dceb302900f528458e002c3e914d6b29e5e0"
   "924b0bcab2dd053f65d9d4a8eea8269c85c419dba640a88e14dc5f8c8c1a4269"
   "7a5ac4594b36f923110f91d1803d385540c01a433140b06054c77a144ee3a6a6"
   "5950c20f9215be3473b1002eb6b1756a22fbc18d21efacbbc8c270c66cf74982"
   "e24f057825cab51c0dd840a4f2d059032239c33e3f52c6ca06fe49bf4f60cc28"
   "a0fb1173d2ee05a141d30e8ffa32dbb86c1aeb5b309f76c2e462965612ec929a"
   "0d3b04acfa4525912c76f765e948be71f505d619cc673a889f0ed9e1d75f237b"
   "7af6a68550253cb4c3a8ff16c8091dbcbdea0ff8eee3d5bd92f49c53c5a15c93"},
  {"IFX TPM EK Intermediate CA 14",
   "D5B2EB8F8F23DD0B5CA0C15D4376E27A0380FD8EB1E52C2C270D961E8C0F66FD"
   "62E6ED6B3660FFBD8B0735179476F5E9C2EA4C762F5FEEDD3B5EB91785A724BC"
   "4C0617B83966336DD9DC407640871BF99DF4E1701EB5A1F5647FC57879CBB973"
   "B2A72BABA8536B2646A37AA5B73E32A4C8F03E35C8834B391AD363F1F7D1DF2B"
   "EE39233F47384F3E2D2E8EF83C9539B4DFC360C8AEB88B6111E757AF646DC01A"
   "68DAA908C7F8068894E9E991C59005068DD9B0F87113E6A80AB045DB4C1B23FF"
   "38A106098C2E184E1CF42A43EA68753F2649999048E8A3C3406032BEB1457070"
   "BCBE3A93E122638F6F18FF505C35FB827CE5D0C12F27F45C0F59C8A4A8697849"},
  {"IFX TPM EK Intermediate CA 16",
   "B98D42D5284620036A6613ED05A1BE11431AE7DE435EC55F72814652B9265EC2"
   "9035D401B538A9C84BB5B875450FAE8FBEDEF3430C4108D8516404F3DE4D4615"
   "2F471013673A7C7F236304C7363B91C0E0FD9FC7A9EC751521A60A6042839CF7"
   "7AEDE3243D0F51F47ACC39676D236BD5298E18B9A4783C60B2A1CD1B32124909"
   "D5844649EE4539D6AA05A5902C147B4F062D5145708EAE224EC65A8B51D7A418"
   "6327DA8F3B9E7C796F8B2DB3D2BDB39B829BDEBA8D2BF882CBADDB75D76FA8FA"
   "313682688BCD2835533A3A68A4AFDF7E597D8B965402FF22A5A4A418FDB4B549"
   "F218C3908E66BDCEAB3E2FE5EE0A4A1D9EB41A286ED07B6C112581FDAEA088D9"},
  {"IFX TPM EK Intermediate CA 17",
   "B0F3CC6F02E8C0486501102731069644A815F631ED41676C05CE3F7E5E5E40DF"
   "B3BF6D99787F2A9BE8F8B8035C03D5C2226072985230D4CE8407ACD6403F72E1"
   "A4DBF069504E56FA8C0807A704526EAC1E379AE559EB4BBAD9DB4E652B3B14E5"
   "38497A5E7768BCE0BFFAF800C61F1F2262775C526E1790A2BECF9A072A58F6A0"
   "F3042B5279FE9957BCADC3C9725428B66B15D5263F00C528AC47716DE6938199"
   "0FF23BC28F2C33B72D89B5F8EEEF9053B60D230431081D656EA8EC16C7CEFD9E"
   "F5A9061A3C921394D453D9AC77397D59B4C3BAF258266F65559469C3007987D5"
   "A8338E10FC54CD930303C37007D6E1E6C63F36BCFBA1E494AFB3ECD9A2407FF9"},
  {"NTC TPM EK Root CA 01",
   "e836ac61b43e3252d5e1a8a4061997a6a0a272ba3d519d6be6360cc8b4b79e8c"
   "d53c07a7ce9e9310ca84b82bbdad32184544ada357d458cf224c4a3130c97d00"
   "4933b5db232d8b6509412eb4777e9e1b093c58b82b1679c84e57a6b218b4d61f"
   "6dd4c3a66b2dd33b52cb1ffdff543289fa36dd71b7c83b66c1aae37caf7fe88d"
   "851a3523e3ea92b59a6b0ca095c5e1d191484c1bff8a33048c3976e826d4c12a"
   "e198f7199d183e0e70c8b46e8106edec3914397e051ae2b9a7f0b4bb9cd7f2ed"
   "f71064eb0eb473df27b7ccef9a018d715c5fe6ab012a8315f933c7f4fc35d34c"
   "efc27de224b2e3de3b3ba316d5df8b90b2eb879e219d270141b78dbb671a3a05"},
  {"STM TPM EK Intermediate CA 03",
   "a5152b4fbd2c70c0c9a0dd919f48ddcde2b5c0c9988cff3b04ecd844f6cc0035"
   "6c4e01b52463deb5179f36acf0c06d4574327c37572292fcd0f272c2d45ea7f2"
   "2e8d8d18aa62354c279e03be9220f0c3822d16de1ea1c130b59afc56e08f22f1"
   "902a07f881ebea3703badaa594ecbdf8fd1709211ba16769f73e76f348e2755d"
   "bba2f94c1869ef71e726f56f8ece987f345c622e8b5c2a5466d41093c0dc2982"
   "e6203d96f539b542347a08e87fc6e248a346d61a505f52add7f768a5203d70b8"
   "68b6ec92ef7a83a4e6d1e1d259018705755d812175489fae83c4ab2957f69a99"
   "9394ac7a243a5c1cd85f92b8648a8e0d23165fdd86fad06990bfd16fb3293379"}
};

const Attestation::PCRValue Attestation::kKnownPCRValues[] = {
  { false, false, kVerified  },
  { false, false, kDeveloper },
  { false, true,  kVerified  },
  { false, true,  kDeveloper },
  { true,  false, kVerified  },
  { true,  false, kDeveloper },
  { true,  true,  kVerified  },
  { true,  true,  kDeveloper }
};

Attestation::Attestation(Tpm* tpm, Platform* platform)
    : tpm_(tpm),
      platform_(platform),
      database_path_(kDefaultDatabasePath),
      thread_(base::kNullThreadHandle),
      pkcs11_key_store_(new Pkcs11KeyStore()),
      user_key_store_(pkcs11_key_store_.get()) {}

Attestation::~Attestation() {
  if (thread_ != base::kNullThreadHandle)
    base::PlatformThread::Join(thread_);
  ClearDatabase();
}

void Attestation::Initialize() {
  base::AutoLock lock(database_pb_lock_);
  if (tpm_) {
    EncryptedData encrypted_db;
    if (!LoadDatabase(&encrypted_db)) {
      LOG(INFO) << "Attestation: Attestation data not found.";
      return;
    }
    if (!DecryptDatabase(encrypted_db, &database_pb_)) {
      LOG(WARNING) << "Attestation: Attestation data invalid.  "
                      "This is normal if the TPM has been cleared.";
      return;
    }
    LOG(INFO) << "Attestation: Valid attestation data exists.";
    // Make sure the owner password is not being held on our account.
    tpm_->RemoveOwnerDependency(Tpm::kAttestation);
  }
}

bool Attestation::IsPreparedForEnrollment() {
  base::AutoLock lock(database_pb_lock_);
  return database_pb_.has_credentials();
}

bool Attestation::IsEnrolled() {
  base::AutoLock lock(database_pb_lock_);
  return (database_pb_.has_identity_key() &&
          database_pb_.identity_key().has_identity_credential());
}

void Attestation::PrepareForEnrollment() {
  // If there is no TPM, we have no work to do.
  if (!tpm_)
    return;
  if (IsPreparedForEnrollment())
    return;
  base::TimeTicks start = base::TimeTicks::Now();
  LOG(INFO) << "Attestation: Preparing for enrollment...";
  SecureBlob ek_public_key;
  if (!tpm_->GetEndorsementPublicKey(&ek_public_key)) {
    LOG(ERROR) << "Attestation: Failed to get EK public key.";
    return;
  }
  // Create an AIK.
  SecureBlob identity_public_key_der;
  SecureBlob identity_public_key;
  SecureBlob identity_key_blob;
  SecureBlob identity_binding;
  SecureBlob identity_label;
  SecureBlob pca_public_key;
  SecureBlob endorsement_credential;
  SecureBlob platform_credential;
  SecureBlob conformance_credential;
  if (!tpm_->MakeIdentity(&identity_public_key_der, &identity_public_key,
                          &identity_key_blob, &identity_binding,
                          &identity_label, &pca_public_key,
                          &endorsement_credential, &platform_credential,
                          &conformance_credential)) {
    LOG(ERROR) << "Attestation: Failed to make AIK.";
    return;
  }

  // Quote PCR0.
  SecureBlob external_data;
  if (!tpm_->GetRandomData(kQuoteExternalDataSize, &external_data)) {
    LOG(ERROR) << "Attestation: GetRandomData failed.";
    return;
  }
  SecureBlob quoted_pcr_value;
  SecureBlob quoted_data;
  SecureBlob quote;
  if (!tpm_->QuotePCR0(identity_key_blob, external_data, &quoted_pcr_value,
                       &quoted_data, &quote)) {
    LOG(ERROR) << "Attestation: Failed to generate quote.";
    return;
  }

  // Create a delegate so we can activate the AIK later.
  SecureBlob delegate_blob;
  SecureBlob delegate_secret;
  if (!tpm_->CreateDelegate(identity_key_blob, &delegate_blob,
                            &delegate_secret)) {
    LOG(ERROR) << "Attestation: Failed to create delegate.";
    return;
  }

  // Assemble a protobuf to store locally.
  base::AutoLock lock(database_pb_lock_);
  TPMCredentials* credentials_pb = database_pb_.mutable_credentials();
  credentials_pb->set_endorsement_public_key(ek_public_key.data(),
                                             ek_public_key.size());
  credentials_pb->set_endorsement_credential(endorsement_credential.data(),
                                             endorsement_credential.size());
  credentials_pb->set_platform_credential(platform_credential.data(),
                                          platform_credential.size());
  credentials_pb->set_conformance_credential(conformance_credential.data(),
                                             conformance_credential.size());
  IdentityKey* key_pb = database_pb_.mutable_identity_key();
  key_pb->set_identity_public_key(identity_public_key_der.data(),
                                  identity_public_key_der.size());
  key_pb->set_identity_key_blob(identity_key_blob.data(),
                                identity_key_blob.size());
  IdentityBinding* binding_pb = database_pb_.mutable_identity_binding();
  binding_pb->set_identity_binding(identity_binding.data(),
                                   identity_binding.size());
  binding_pb->set_identity_public_key_der(identity_public_key_der.data(),
                                          identity_public_key_der.size());
  binding_pb->set_identity_public_key(identity_public_key.data(),
                                      identity_public_key.size());
  binding_pb->set_identity_label(identity_label.data(), identity_label.size());
  binding_pb->set_pca_public_key(pca_public_key.data(), pca_public_key.size());
  Quote* quote_pb = database_pb_.mutable_pcr0_quote();
  quote_pb->set_quote(quote.data(), quote.size());
  quote_pb->set_quoted_data(quoted_data.data(), quoted_data.size());
  quote_pb->set_quoted_pcr_value(quoted_pcr_value.data(),
                                 quoted_pcr_value.size());
  Delegation* delegate_pb = database_pb_.mutable_delegate();
  delegate_pb->set_blob(delegate_blob.data(), delegate_blob.size());
  delegate_pb->set_secret(delegate_secret.data(), delegate_secret.size());

  if (!tpm_->GetRandomData(kCipherKeySize, &database_key_)) {
    LOG(ERROR) << "Attestation: GetRandomData failed.";
    return;
  }
  SecureBlob sealed_key;
  if (!tpm_->SealToPCR0(database_key_, &sealed_key)) {
    LOG(ERROR) << "Attestation: Failed to seal cipher key.";
    return;
  }
  EncryptedData encrypted_pb;
  encrypted_pb.set_wrapped_key(sealed_key.data(), sealed_key.size());
  if (!EncryptDatabase(database_pb_, &encrypted_pb)) {
    LOG(ERROR) << "Attestation: Failed to encrypt db.";
    return;
  }
  if (!StoreDatabase(encrypted_pb)) {
    LOG(ERROR) << "Attestation: Failed to store db.";
    return;
  }
  tpm_->RemoveOwnerDependency(Tpm::kAttestation);
  base::TimeDelta delta = (base::TimeTicks::Now() - start);
  LOG(INFO) << "Attestation: Prepared successfully (" << delta.InMilliseconds()
            << "ms).";
}

bool Attestation::Verify() {
  if (!tpm_)
    return false;
  LOG(INFO) << "Attestation: Verifying data.";
  base::AutoLock lock(database_pb_lock_);
  const TPMCredentials& credentials = database_pb_.credentials();
  SecureBlob ek_public_key = ConvertStringToBlob(
      credentials.endorsement_public_key());
  if (!VerifyEndorsementCredential(
          ConvertStringToBlob(credentials.endorsement_credential()),
          ek_public_key)) {
    LOG(ERROR) << "Attestation: Bad endorsement credential.";
    return false;
  }
  if (!VerifyIdentityBinding(database_pb_.identity_binding())) {
    LOG(ERROR) << "Attestation: Bad identity binding.";
    return false;
  }
  SecureBlob aik_public_key = ConvertStringToBlob(
      database_pb_.identity_binding().identity_public_key_der());
  if (!VerifyQuote(aik_public_key, database_pb_.pcr0_quote())) {
    LOG(ERROR) << "Attestation: Bad PCR0 quote.";
    return false;
  }
  SecureBlob nonce;
  if (!tpm_->GetRandomData(kNonceSize, &nonce)) {
    LOG(ERROR) << "Attestation: GetRandomData failed.";
    return false;
  }
  SecureBlob identity_key_blob = ConvertStringToBlob(
      database_pb_.identity_key().identity_key_blob());
  SecureBlob public_key;
  SecureBlob public_key_der;
  SecureBlob key_blob;
  SecureBlob key_info;
  SecureBlob proof;
  if (!tpm_->CreateCertifiedKey(identity_key_blob, nonce,
                                &public_key, &public_key_der,
                                &key_blob, &key_info, &proof)) {
    LOG(ERROR) << "Attestation: Failed to create certified key.";
    return false;
  }
  if (!VerifyCertifiedKey(aik_public_key, public_key_der, key_info, proof)) {
    LOG(ERROR) << "Attestation: Bad certified key.";
    return false;
  }
  SecureBlob delegate_blob =
      ConvertStringToBlob(database_pb_.delegate().blob());
  SecureBlob delegate_secret =
      ConvertStringToBlob(database_pb_.delegate().secret());
  SecureBlob aik_public_key_tpm = ConvertStringToBlob(
      database_pb_.identity_binding().identity_public_key());
  if (!VerifyActivateIdentity(delegate_blob, delegate_secret,
                              identity_key_blob, aik_public_key_tpm,
                              ek_public_key)) {
    LOG(ERROR) << "Attestation: Failed to verify owner delegation.";
    return false;
  }
  LOG(INFO) << "Attestation: Verified OK.";
  return true;
}

bool Attestation::VerifyEK() {
  SecureBlob ek_cert;
  if (!tpm_->GetEndorsementCredential(&ek_cert)) {
    LOG(ERROR) << __func__ << ": Failed to get EK cert.";
    return false;
  }
  SecureBlob ek_public_key;
  if (!tpm_->GetEndorsementPublicKey(&ek_public_key)) {
    LOG(ERROR) << __func__ << ": Failed to get EK public key.";
    return false;
  }
  return VerifyEndorsementCredential(ek_cert, ek_public_key);
}

bool Attestation::CreateEnrollRequest(SecureBlob* pca_request) {
  if (!IsPreparedForEnrollment()) {
    LOG(ERROR) << __func__ << ": Enrollment is not possible, attestation data "
               << "does not exist.";
    return false;
  }
  base::AutoLock lock(database_pb_lock_);
  AttestationEnrollmentRequest request_pb;
  if (!EncryptEndorsementCredential(
      ConvertStringToBlob(database_pb_.credentials().endorsement_credential()),
      request_pb.mutable_encrypted_endorsement_credential())) {
    LOG(ERROR) << __func__ << ": Failed to encrypt EK cert.";
    return false;
  }
  request_pb.set_identity_public_key(
      database_pb_.identity_binding().identity_public_key());
  *request_pb.mutable_pcr0_quote() = database_pb_.pcr0_quote();
  string tmp;
  if (!request_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  *pca_request = ConvertStringToBlob(tmp);
  return true;
}

bool Attestation::Enroll(const SecureBlob& pca_response) {
  AttestationEnrollmentResponse response_pb;
  if (!response_pb.ParseFromArray(pca_response.const_data(),
                                  pca_response.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse response from Privacy CA.";
    return false;
  }
  if (response_pb.status() != OK) {
    LOG(ERROR) << __func__ << ": Error received from Privacy CA: "
               << response_pb.detail();
    return false;
  }
  SecureBlob delegate_blob = ConvertStringToBlob(
      database_pb_.delegate().blob());
  SecureBlob delegate_secret = ConvertStringToBlob(
      database_pb_.delegate().secret());
  SecureBlob aik_blob = ConvertStringToBlob(
      database_pb_.identity_key().identity_key_blob());
  SecureBlob encrypted_asym = ConvertStringToBlob(
      response_pb.encrypted_identity_credential().asym_ca_contents());
  SecureBlob encrypted_sym = ConvertStringToBlob(
      response_pb.encrypted_identity_credential().sym_ca_attestation());
  SecureBlob aik_credential;
  if (!tpm_->ActivateIdentity(delegate_blob, delegate_secret,
                              aik_blob, encrypted_asym, encrypted_sym,
                              &aik_credential)) {
    LOG(ERROR) << __func__ << ": Failed to activate identity.";
    return false;
  }
  database_pb_.mutable_identity_key()->set_identity_credential(
      ConvertBlobToString(aik_credential));
  // TODO(dkrahn): Remove credentials and identity_binding from the database.
  if (!PersistDatabaseChanges()) {
    LOG(ERROR) << __func__ << ": Failed to persist database changes.";
    return false;
  }
  LOG(INFO) << "Attestation: Enrollment complete.";
  return true;
}

bool Attestation::CreateCertRequest(bool include_stable_id,
                                    bool include_device_state,
                                    SecureBlob* pca_request) {
  if (!IsEnrolled()) {
    LOG(ERROR) << __func__ << ": Device is not enrolled for attestation.";
    return false;
  }
  AttestationCertificateRequest request_pb;
  string message_id(kNonceSize, 0);
  CryptoLib::GetSecureRandom(
      reinterpret_cast<unsigned char*>(string_as_array(&message_id)),
      kNonceSize);
  request_pb.set_message_id(message_id);
  request_pb.set_identity_credential(
      database_pb_.identity_key().identity_credential());
  request_pb.set_include_stable_id(include_stable_id);
  request_pb.set_include_device_state(include_device_state);
  SecureBlob nonce;
  if (!tpm_->GetRandomData(kNonceSize, &nonce)) {
    LOG(ERROR) << __func__ << ": GetRandomData failed.";
    return false;
  }
  SecureBlob identity_key_blob = ConvertStringToBlob(
      database_pb_.identity_key().identity_key_blob());
  SecureBlob public_key;
  SecureBlob public_key_der;
  SecureBlob key_blob;
  SecureBlob key_info;
  SecureBlob proof;
  if (!tpm_->CreateCertifiedKey(identity_key_blob, nonce,
                                &public_key, &public_key_der,
                                &key_blob, &key_info, &proof)) {
    LOG(ERROR) << __func__ << ": Failed to create certified key.";
    return false;
  }
  request_pb.set_certified_public_key(ConvertBlobToString(public_key));
  request_pb.set_certified_key_info(ConvertBlobToString(key_info));
  request_pb.set_certified_key_proof(ConvertBlobToString(proof));
  string tmp;
  if (!request_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  *pca_request = ConvertStringToBlob(tmp);
  ClearString(&tmp);
  // Save certified key blob so we can finish the operation later.
  CertifiedKey certified_key_pb;
  certified_key_pb.set_key_blob(ConvertBlobToString(key_blob));
  certified_key_pb.set_public_key(ConvertBlobToString(public_key_der));
  if (!certified_key_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  pending_cert_requests_[message_id] = ConvertStringToBlob(tmp);
  ClearString(&tmp);
  return true;
}

bool Attestation::FinishCertRequest(const SecureBlob& pca_response,
                                    bool is_user_key,
                                    const string& key_name,
                                    SecureBlob* certificate) {
  AttestationCertificateResponse response_pb;
  if (!response_pb.ParseFromArray(pca_response.const_data(),
                                  pca_response.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse response from Privacy CA.";
    return false;
  }
  CertRequestMap::iterator iter = pending_cert_requests_.find(
      response_pb.message_id());
  if (iter == pending_cert_requests_.end()) {
    LOG(ERROR) << __func__ << ": Pending request not found.";
    return false;
  }
  if (response_pb.status() != OK) {
    LOG(ERROR) << __func__ << ": Error received from Privacy CA: "
               << response_pb.detail();
    pending_cert_requests_.erase(iter);
    return false;
  }
  CertifiedKey certified_key_pb;
  if (!certified_key_pb.ParseFromArray(iter->second.const_data(),
                                       iter->second.size())) {
    LOG(ERROR) << __func__ << ": Failed to parse pending request.";
    pending_cert_requests_.erase(iter);
    return false;
  }
  pending_cert_requests_.erase(iter);

  // The PCA issued a certificate and the response matched a pending request.
  // Now we want to finish populating the CertifiedKey and store it for later.
  certified_key_pb.set_certified_key_credential(
      response_pb.certified_key_credential());
  certified_key_pb.set_intermediate_ca_cert(response_pb.intermediate_ca_cert());
  certified_key_pb.set_key_name(key_name);
  string tmp;
  if (!certified_key_pb.SerializeToString(&tmp)) {
    LOG(ERROR) << __func__ << ": Failed to serialize protobuf.";
    return false;
  }
  SecureBlob certified_key = ConvertStringToBlob(tmp);
  ClearString(&tmp);
  bool result = false;
  if (is_user_key) {
    result = user_key_store_->Write(key_name, certified_key);
  } else {
    result = AddDeviceKey(key_name, certified_key);
  }
  if (!result) {
    LOG(ERROR) << __func__ << ": Failed to store certified key.";
    return false;
  }
  *certificate = ConvertStringToBlob(response_pb.certified_key_credential());
  LOG(INFO) << "Attestation: Certified key credential received and stored.";
  return true;
}

SecureBlob Attestation::ConvertStringToBlob(const string& s) {
  return SecureBlob(s.data(), s.length());
}

string Attestation::ConvertBlobToString(const chromeos::Blob& blob) {
  return string(reinterpret_cast<const char*>(&blob.front()), blob.size());
}

SecureBlob Attestation::SecureCat(const SecureBlob& blob1,
                                  const SecureBlob& blob2) {
  SecureBlob result(blob1.size() + blob2.size());
  unsigned char* buffer = vector_as_array(&result);
  memcpy(buffer, blob1.const_data(), blob1.size());
  memcpy(buffer + blob1.size(), blob2.const_data(), blob2.size());
  return SecureBlob(result.begin(), result.end());
}

bool Attestation::EncryptDatabase(const AttestationDatabase& db,
                                  EncryptedData* encrypted_db) {
  SecureBlob iv;
  if (!tpm_->GetRandomData(kCipherBlockSize, &iv)) {
    LOG(ERROR) << "GetRandomData failed.";
    return false;
  }
  string serial_string;
  if (!db.SerializeToString(&serial_string)) {
    LOG(ERROR) << "Failed to serialize db.";
    return false;
  }
  SecureBlob serial_data(serial_string.data(), serial_string.size());
  SecureBlob encrypted_data;
  if (!CryptoLib::AesEncrypt(serial_data, database_key_, iv, &encrypted_data)) {
    LOG(ERROR) << "Failed to encrypt db.";
    return false;
  }
  encrypted_db->set_encrypted_data(encrypted_data.data(),
                                   encrypted_data.size());
  encrypted_db->set_iv(iv.data(), iv.size());
  encrypted_db->set_mac(ComputeHMAC(*encrypted_db, database_key_));
  return true;
}

bool Attestation::DecryptDatabase(const EncryptedData& encrypted_db,
                                  AttestationDatabase* db) {
  SecureBlob sealed_key(encrypted_db.wrapped_key().data(),
                        encrypted_db.wrapped_key().length());
  if (!tpm_->Unseal(sealed_key, &database_key_)) {
    LOG(ERROR) << "Cannot unseal database key.";
    return false;
  }
  string mac = ComputeHMAC(encrypted_db, database_key_);
  if (mac.length() != encrypted_db.mac().length()) {
    LOG(ERROR) << "Corrupted database.";
    return false;
  }
  if (0 != chromeos::SafeMemcmp(mac.data(), encrypted_db.mac().data(),
                                mac.length())) {
    LOG(ERROR) << "Corrupted database.";
    return false;
  }
  SecureBlob iv(encrypted_db.iv().data(), encrypted_db.iv().length());
  SecureBlob encrypted_data(encrypted_db.encrypted_data().data(),
                            encrypted_db.encrypted_data().length());
  SecureBlob serial_db;
  if (!CryptoLib::AesDecrypt(encrypted_data, database_key_, iv, &serial_db)) {
    LOG(ERROR) << "Failed to decrypt database.";
    return false;
  }
  if (!db->ParseFromArray(serial_db.data(), serial_db.size())) {
    LOG(ERROR) << "Failed to parse database.";
    return false;
  }
  return true;
}

string Attestation::ComputeHMAC(const EncryptedData& encrypted_data,
                                const SecureBlob& hmac_key) {
  SecureBlob hmac_input = SecureCat(
      ConvertStringToBlob(encrypted_data.iv()),
      ConvertStringToBlob(encrypted_data.encrypted_data()));
  return ConvertBlobToString(CryptoLib::HmacSha512(hmac_key, hmac_input));
}

bool Attestation::StoreDatabase(const EncryptedData& encrypted_db) {
  string database_serial;
  if (!encrypted_db.SerializeToString(&database_serial)) {
    LOG(ERROR) << "Failed to serialize encrypted db.";
    return false;
  }
  if (!platform_->WriteStringToFile(database_path_.value(), database_serial)) {
    LOG(ERROR) << "Failed to write db.";
    return false;
  }
  CheckDatabasePermissions();
  return true;
}

bool Attestation::LoadDatabase(EncryptedData* encrypted_db) {
  CheckDatabasePermissions();
  string serial;
  if (!platform_->ReadFileToString(database_path_.value(), &serial)) {
    return false;
  }
  if (!encrypted_db->ParseFromString(serial)) {
    LOG(ERROR) << "Failed to parse encrypted db.";
    return false;
  }
  return true;
}

bool Attestation::PersistDatabaseChanges() {
  // Load the existing encrypted structure so we don't need to re-seal the key.
  EncryptedData encrypted_db;
  if (!LoadDatabase(&encrypted_db))
    return false;
  if (!EncryptDatabase(database_pb_, &encrypted_db))
    return false;
  return StoreDatabase(encrypted_db);
}

void Attestation::CheckDatabasePermissions() {
  const mode_t kMask = 0007;  // No permissions for 'others'.
  CHECK(platform_);
  mode_t permissions = 0;
  if (!platform_->GetPermissions(database_path_.value(), &permissions))
    return;
  if ((permissions & kMask) == 0)
    return;
  platform_->SetPermissions(database_path_.value(), permissions & ~kMask);
}

bool Attestation::VerifyEndorsementCredential(const SecureBlob& credential,
                                              const SecureBlob& public_key) {
  const unsigned char* asn1_ptr = &credential.front();
  X509* x509 = d2i_X509(NULL, &asn1_ptr, credential.size());
  if (!x509) {
    LOG(ERROR) << "Failed to parse endorsement credential.";
    return false;
  }
  // Manually verify the certificate signature.
  char issuer[100];  // A longer CN will truncate.
  X509_NAME_get_text_by_NID(x509->cert_info->issuer, NID_commonName, issuer,
                            arraysize(issuer));
  EVP_PKEY* issuer_key = GetAuthorityPublicKey(issuer);
  if (!issuer_key) {
    LOG(ERROR) << "Unknown endorsement credential issuer.";
    return false;
  }
  if (X509_verify(x509, issuer_key) != 1) {
    LOG(ERROR) << "Bad endorsement credential signature.";
    EVP_PKEY_free(issuer_key);
    return false;
  }
  EVP_PKEY_free(issuer_key);
  // Verify that the given public key matches the public key in the credential.
  // Note: Do not use any openssl functions that attempt to decode the public
  // key. These will fail because openssl does not recognize the OAEP key type.
  SecureBlob credential_public_key(x509->cert_info->key->public_key->data,
                                   x509->cert_info->key->public_key->length);
  if (credential_public_key.size() != public_key.size() ||
      memcmp(&credential_public_key.front(),
             &public_key.front(),
             public_key.size()) != 0) {
    LOG(ERROR) << "Bad endorsement credential public key.";
    return false;
  }
  X509_free(x509);
  return true;
}

bool Attestation::VerifyIdentityBinding(const IdentityBinding& binding) {
  // Reconstruct and hash a serialized TPM_IDENTITY_CONTENTS structure.
  const unsigned char header[] = {1, 1, 0, 0, 0, 0, 0, 0x79};
  string label_ca = binding.identity_label() + binding.pca_public_key();
  SecureBlob label_ca_digest = CryptoLib::Sha1(ConvertStringToBlob(label_ca));
  ClearString(&label_ca);
  // The signed data is header + digest + pubkey.
  SecureBlob contents = SecureCat(SecureCat(
      SecureBlob(header, arraysize(header)),
      label_ca_digest),
      ConvertStringToBlob(binding.identity_public_key()));
  // Now verify the signature.
  if (!VerifySignature(ConvertStringToBlob(binding.identity_public_key_der()),
                       contents,
                       ConvertStringToBlob(binding.identity_binding()))) {
    LOG(ERROR) << "Failed to verify identity binding signature.";
    return false;
  }
  return true;
}

bool Attestation::VerifyQuote(const SecureBlob& aik_public_key,
                              const Quote& quote) {
  if (!VerifySignature(aik_public_key,
                       ConvertStringToBlob(quote.quoted_data()),
                       ConvertStringToBlob(quote.quote()))) {
    LOG(ERROR) << "Failed to verify quote signature.";
    return false;
  }

  // Check that the quoted value matches the given PCR value. We can verify this
  // by reconstructing the TPM_PCR_COMPOSITE structure the TPM would create.
  const uint8_t header[] = {
    static_cast<uint8_t>(0), static_cast<uint8_t>(2),
    static_cast<uint8_t>(1), static_cast<uint8_t>(0),
    static_cast<uint8_t>(0), static_cast<uint8_t>(0),
    static_cast<uint8_t>(0),
    static_cast<uint8_t>(quote.quoted_pcr_value().size())};
  SecureBlob pcr_composite = SecureCat(
      SecureBlob(header, arraysize(header)),
      ConvertStringToBlob(quote.quoted_pcr_value()));
  SecureBlob pcr_digest = CryptoLib::Sha1(pcr_composite);
  SecureBlob quoted_data = ConvertStringToBlob(quote.quoted_data());
  if (search(quoted_data.begin(), quoted_data.end(),
             pcr_digest.begin(), pcr_digest.end()) == quoted_data.end()) {
    LOG(ERROR) << "PCR0 value mismatch.";
    return false;
  }

  // Check if the PCR0 value represents a known mode.
  for (size_t i = 0; i < arraysize(kKnownPCRValues); ++i) {
    SecureBlob settings_blob(3);
    settings_blob[0] = kKnownPCRValues[i].developer_mode_enabled;
    settings_blob[1] = kKnownPCRValues[i].recovery_mode_enabled;
    settings_blob[2] = kKnownPCRValues[i].firmware_type;
    SecureBlob settings_digest = CryptoLib::Sha1(settings_blob);
    chromeos::Blob extend_pcr_value(kDigestSize, 0);
    extend_pcr_value.insert(extend_pcr_value.end(), settings_digest.begin(),
                            settings_digest.end());
    SecureBlob final_pcr_value = CryptoLib::Sha1(extend_pcr_value);
    if (quote.quoted_pcr_value().size() == final_pcr_value.size() &&
        0 == memcmp(quote.quoted_pcr_value().data(), final_pcr_value.data(),
                    final_pcr_value.size())) {
      string description = "Developer Mode: ";
      description += kKnownPCRValues[i].developer_mode_enabled ? "On" : "Off";
      description += ", Recovery Mode: ";
      description += kKnownPCRValues[i].recovery_mode_enabled ? "On" : "Off";
      description += ", Firmware Type: ";
      description += (kKnownPCRValues[i].firmware_type == 1) ? "Verified" :
                     "Developer";
      LOG(INFO) << "PCR0: " << description;
      return true;
    }
  }
  LOG(WARNING) << "PCR0 value not recognized.";
  return true;
}

bool Attestation::VerifyCertifiedKey(
    const SecureBlob& aik_public_key,
    const SecureBlob& certified_public_key,
    const SecureBlob& certified_key_info,
    const SecureBlob& proof) {
  string key_info = ConvertBlobToString(certified_key_info);
  if (!VerifySignature(aik_public_key, certified_key_info, proof)) {
    LOG(ERROR) << "Failed to verify certified key proof signature.";
    return false;
  }
  const unsigned char* asn1_ptr = &certified_public_key.front();
  RSA* rsa = d2i_RSAPublicKey(NULL, &asn1_ptr, certified_public_key.size());
  if (!rsa) {
    LOG(ERROR) << "Failed to decode certified public key.";
    return false;
  }
  SecureBlob modulus(BN_num_bytes(rsa->n));
  BN_bn2bin(rsa->n, vector_as_array(&modulus));
  RSA_free(rsa);
  SecureBlob key_digest = CryptoLib::Sha1(modulus);
  if (std::search(certified_key_info.begin(),
                  certified_key_info.end(),
                  key_digest.begin(),
                  key_digest.end()) == certified_key_info.end()) {
    LOG(ERROR) << "Cerified public key mismatch.";
    return false;
  }
  return true;
}

EVP_PKEY* Attestation::GetAuthorityPublicKey(const char* issuer_name) {
  const int kNumIssuers = arraysize(kKnownEndorsementCA);
  for (int i = 0; i < kNumIssuers; ++i) {
    if (0 == strcmp(issuer_name, kKnownEndorsementCA[i].issuer)) {
      RSA* rsa = RSA_new();
      if (!rsa)
        return NULL;
      rsa->e = BN_new();
      if (!rsa->e) {
        RSA_free(rsa);
        return NULL;
      }
      BN_set_word(rsa->e, kWellKnownExponent);
      rsa->n = BN_new();
      if (!rsa->n) {
        RSA_free(rsa);
        return NULL;
      }
      BN_hex2bn(&rsa->n, kKnownEndorsementCA[i].modulus);
      EVP_PKEY* pkey = EVP_PKEY_new();
      if (!pkey) {
        RSA_free(rsa);
        return NULL;
      }
      EVP_PKEY_assign_RSA(pkey, rsa);
      return pkey;
    }
  }
  return NULL;
}

bool Attestation::VerifySignature(const SecureBlob& public_key,
                                  const SecureBlob& signed_data,
                                  const SecureBlob& signature) {
  const unsigned char* asn1_ptr = &public_key.front();
  RSA* rsa = d2i_RSAPublicKey(NULL, &asn1_ptr, public_key.size());
  if (!rsa) {
    LOG(ERROR) << "Failed to decode public key.";
    return false;
  }
  SecureBlob digest = CryptoLib::Sha1(signed_data);
  if (!RSA_verify(NID_sha1, &digest.front(), digest.size(),
                  const_cast<unsigned char*>(&signature.front()),
                  signature.size(), rsa)) {
    LOG(ERROR) << "Failed to verify signature.";
    RSA_free(rsa);
    return false;
  }
  RSA_free(rsa);
  return true;
}

void Attestation::ClearDatabase() {
  if (database_pb_.has_credentials()) {
    TPMCredentials* credentials = database_pb_.mutable_credentials();
    ClearString(credentials->mutable_endorsement_public_key());
    ClearString(credentials->mutable_endorsement_credential());
    ClearString(credentials->mutable_platform_credential());
    ClearString(credentials->mutable_conformance_credential());
  }
  if (database_pb_.has_identity_binding()) {
    IdentityBinding* binding = database_pb_.mutable_identity_binding();
    ClearString(binding->mutable_identity_binding());
    ClearString(binding->mutable_identity_public_key_der());
    ClearString(binding->mutable_identity_public_key());
    ClearString(binding->mutable_identity_label());
    ClearString(binding->mutable_pca_public_key());
  }
  if (database_pb_.has_identity_key()) {
    IdentityKey* key = database_pb_.mutable_identity_key();
    ClearString(key->mutable_identity_public_key());
    ClearString(key->mutable_identity_key_blob());
    ClearString(key->mutable_identity_credential());
  }
  if (database_pb_.has_pcr0_quote()) {
    Quote* quote = database_pb_.mutable_pcr0_quote();
    ClearString(quote->mutable_quote());
    ClearString(quote->mutable_quoted_data());
    ClearString(quote->mutable_quoted_pcr_value());
  }
  if (database_pb_.has_delegate()) {
    Delegation* delegate = database_pb_.mutable_delegate();
    ClearString(delegate->mutable_blob());
    ClearString(delegate->mutable_secret());
  }
}

void Attestation::ClearString(string* s) {
  chromeos::SecureMemset(string_as_array(s), 0, s->length());
  s->clear();
}

bool Attestation::VerifyActivateIdentity(const SecureBlob& delegate_blob,
                                         const SecureBlob& delegate_secret,
                                         const SecureBlob& identity_key_blob,
                                         const SecureBlob& identity_public_key,
                                         const SecureBlob& ek_public_key) {
  const char* kTestCredential = "test";
  const uint8_t kAlgAES256 = 9;  // This comes from TPM_ALG_AES256.
  const uint8_t kEncModeCBC = 2;  // This comes from TPM_SYM_MODE_CBC.
  const uint8_t kAsymContentHeader[] =
      {0, 0, 0, kAlgAES256, 0, kEncModeCBC, 0, kCipherKeySize};
  const uint8_t kSymContentHeader[12] = {0};

  // Generate an AES key and encrypt the credential.
  SecureBlob aes_key(kCipherKeySize);
  CryptoLib::GetSecureRandom(vector_as_array(&aes_key), aes_key.size());
  SecureBlob credential(kTestCredential, strlen(kTestCredential));
  SecureBlob encrypted_credential;
  if (!tpm_->TssCompatibleEncrypt(aes_key, credential, &encrypted_credential)) {
    LOG(ERROR) << "Failed to encrypt credential.";
    return false;
  }

  // Construct a TPM_ASYM_CA_CONTENTS structure.
  SecureBlob public_key_digest = CryptoLib::Sha1(identity_public_key);
  SecureBlob asym_content = SecureCat(SecureCat(
      SecureBlob(kAsymContentHeader, arraysize(kAsymContentHeader)),
      aes_key),
      public_key_digest);

  // Encrypt the TPM_ASYM_CA_CONTENTS with the EK public key.
  const unsigned char* asn1_ptr = &ek_public_key[0];
  RSA* rsa = d2i_RSAPublicKey(NULL, &asn1_ptr, ek_public_key.size());
  if (!rsa) {
    LOG(ERROR) << "Failed to decode EK public key.";
    return false;
  }
  SecureBlob encrypted_asym_content;
  if (!tpm_->TpmCompatibleOAEPEncrypt(rsa, asym_content,
                                      &encrypted_asym_content)) {
    LOG(ERROR) << "Failed to encrypt with EK public key.";
    return false;
  }

  // Construct a TPM_SYM_CA_ATTESTATION structure.
  uint32_t length = htonl(encrypted_credential.size());
  SecureBlob length_blob(sizeof(uint32_t));
  memcpy(length_blob.data(), &length, sizeof(uint32_t));
  SecureBlob sym_content = SecureCat(SecureCat(
      length_blob,
      SecureBlob(kSymContentHeader, arraysize(kSymContentHeader))),
      encrypted_credential);

  // Attempt to activate the identity.
  SecureBlob credential_out;
  if (!tpm_->ActivateIdentity(delegate_blob, delegate_secret, identity_key_blob,
                              encrypted_asym_content, sym_content,
                              &credential_out)) {
    LOG(ERROR) << "Failed to activate identity.";
    return false;
  }
  if (credential.size() != credential_out.size() ||
      chromeos::SafeMemcmp(credential.data(), credential_out.data(),
                           credential.size()) != 0) {
    LOG(ERROR) << "Invalid identity credential.";
    return false;
  }
  return true;
}

bool Attestation::EncryptEndorsementCredential(
    const SecureBlob& credential,
    EncryptedData* encrypted_credential) {
  // Encrypt the credential with a generated AES key.
  SecureBlob aes_key;
  if (!tpm_->GetRandomData(kCipherKeySize, &aes_key)) {
    LOG(ERROR) << "GetRandomData failed.";
    return false;
  }
  SecureBlob aes_iv;
  if (!tpm_->GetRandomData(kCipherBlockSize, &aes_iv)) {
    LOG(ERROR) << "GetRandomData failed.";
    return false;
  }
  SecureBlob encrypted_data;
  if (!CryptoLib::AesEncrypt(credential, aes_key, aes_iv, &encrypted_data)) {
    LOG(ERROR) << "AesEncrypt failed.";
    return false;
  }
  encrypted_credential->set_encrypted_data(encrypted_data.data(),
                                           encrypted_data.size());
  encrypted_credential->set_iv(aes_iv.data(), aes_iv.size());
  encrypted_credential->set_mac(ComputeHMAC(*encrypted_credential, aes_key));

  // Wrap the AES key with the PCA public key.
  RSA* rsa = RSA_new();
  if (!rsa)
    return false;
  rsa->e = BN_new();
  if (!rsa->e) {
    RSA_free(rsa);
    return false;
  }
  BN_set_word(rsa->e, kWellKnownExponent);
  rsa->n = BN_new();
  if (!rsa->n) {
    RSA_free(rsa);
    return false;
  }
  BN_hex2bn(&rsa->n, kDefaultPCAPublicKey);
  string encrypted_key;
  encrypted_key.resize(RSA_size(rsa));
  unsigned char* buffer = reinterpret_cast<unsigned char*>(
      string_as_array(&encrypted_key));
  int length = RSA_public_encrypt(aes_key.size(),
                                  vector_as_array(&aes_key),
                                  buffer, rsa, RSA_PKCS1_OAEP_PADDING);
  if (length == -1) {
    LOG(ERROR) << "RSA_public_encrypt failed.";
    return false;
  }
  encrypted_key.resize(length);
  encrypted_credential->set_wrapped_key(encrypted_key);
  return true;
}

bool Attestation::AddDeviceKey(const std::string& key_name,
                               const chromeos::SecureBlob& key_data) {
  // TODO(dkrahn): implement
  return true;
}

}
