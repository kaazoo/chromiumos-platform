// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <base/strings/string_number_conversions.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#include "libhwsec/backend/tpm1/backend_test_base.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;
using tpm_manager::NvramResult;
using tpm_manager::NvramSpaceAttribute;

namespace hwsec {

namespace {

// Endorsement Certificate in form of TCG_PCCLIENT_STORED_CERT
constexpr char kFakeStoredCert[] =
    "10010004F11002308204EB308203D3A00302010202046C6CDBA1300D06092A864886F70D01"
    "010505003077310B3009060355040613024445310F300D060355040813065361786F6E7931"
    "21301F060355040A1318496E66696E656F6E20546563686E6F6C6F67696573204147310C30"
    "0A060355040B130341494D312630240603550403131D4946582054504D20454B20496E7465"
    "726D656469617465204341203137301E170D3139303430313232323732385A170D32393034"
    "30313232323732385A300030820137302206092A864886F70D0101073015A213301106092A"
    "864886F70D0101090404544350410382010F003082010A0282010100E351E06FC7C14B4C46"
    "265F0CDE1D4327CDBEB2FCF8F8808F307A58EA6560A68915F0609740DFE0EB598502B9B0DC"
    "16D585576CD8B0EE6D0F0A25501B05CF7DFEEB72EDBD92615B0A1C0C84EBC226E998C52792"
    "A5B872E54347DAF60B9B1D89D78AD161DDDA259AA8C704E7B67A558DFC64D9EB10B2F96AFD"
    "D9D8C9373C0A70182F5F78307F9A187DE0D683D716F03F187CDD97A0530BE45AC2FFF7C4D9"
    "4500C9A1C6C50B046A739655BB7A5187226B200C2678E9D5B6EBB9485A599A6637CF01E039"
    "E55E50DF0C6F2B6B20AF418323FFF294898798BA8884F165842CF58C84657DBE4DAEAE2DB9"
    "F4724BEAB4F5C3755CC34F4E08901E2105BBB919A56CFB0F670203010001A38201DF308201"
    "DB30550603551D110101FF044B3049A447304531163014060567810502010C0B69643A3439"
    "34363538303031173015060567810502020C0C534C42393634355454312E32311230100605"
    "67810502030C0769643A38353230300C0603551D130101FF040230003081BC0603551D2001"
    "01FF0481B13081AE3081AB060B6086480186F84501072F0130819B303906082B0601050507"
    "0201162D687474703A2F2F7777772E766572697369676E2E636F6D2F7265706F7369746F72"
    "792F696E6465782E68746D6C305E06082B0601050507020230521E50005400430050004100"
    "20005400720075007300740065006400200050006C006100740066006F0072006D0020004D"
    "006F00640075006C006500200045006E0064006F007200730065006D0065006E0074301F06"
    "03551D23041830168014EC3F8D4CC12ABE88A019064E8A62B7018FA2E3593081930603551D"
    "0904818B308188303A06035504343133300B300906052B0E03021A05003024302206092A86"
    "4886F70D0101073015A213301106092A864886F70D01010904045443504130160605678105"
    "0210310D300B0C03312E32020102020103303206056781050212312930270101FFA0030A01"
    "01A1030A0100A2030A0100A310300E1603332E310A01040A01010101FF0101FF300D06092A"
    "864886F70D010105050003820101003FC27BF941CEBB07E627CCE83DA5F08F5E0CFD0B8786"
    "2CFD6560012B59F738DBFE76F32190B02EA7A63FA678D8CF06736A6836A2FA70F2DC618517"
    "6A80F975823028EB97C5C4881E7C7C215F2AFC7A179A9A02A373017016D8C7C48087F3555D"
    "712DF4167233691443FE8B81DDB6FEE69A49D3805DA89BA1477E892A4041B62B6BAC1EEEC2"
    "80D53C80B346AB80EF8CC6D10E4D1C9AD1A5805E830F89A2FD5C87EE140F053B04E4375754"
    "9F6A26A580D1D317AED00C925FA93328D245772D0B2C74A933C813020EBC558C9F1381BF9F"
    "36E1350C6F1A1B9F7D5630698DBA6F0E2F334D101ACBB20E60963B12FCF61B61A2F76BC6AE"
    "D4FC0F907D248B542F28BCB4";
// Actual expected endorsement certificate extracted from stored cert.
constexpr char kFakeExtractedCert[] =
    "308204EB308203D3A00302010202046C6CDBA1300D06092A864886F70D0101050500307731"
    "0B3009060355040613024445310F300D060355040813065361786F6E793121301F06035504"
    "0A1318496E66696E656F6E20546563686E6F6C6F67696573204147310C300A060355040B13"
    "0341494D312630240603550403131D4946582054504D20454B20496E7465726D6564696174"
    "65204341203137301E170D3139303430313232323732385A170D3239303430313232323732"
    "385A300030820137302206092A864886F70D0101073015A213301106092A864886F70D0101"
    "090404544350410382010F003082010A0282010100E351E06FC7C14B4C46265F0CDE1D4327"
    "CDBEB2FCF8F8808F307A58EA6560A68915F0609740DFE0EB598502B9B0DC16D585576CD8B0"
    "EE6D0F0A25501B05CF7DFEEB72EDBD92615B0A1C0C84EBC226E998C52792A5B872E54347DA"
    "F60B9B1D89D78AD161DDDA259AA8C704E7B67A558DFC64D9EB10B2F96AFDD9D8C9373C0A70"
    "182F5F78307F9A187DE0D683D716F03F187CDD97A0530BE45AC2FFF7C4D94500C9A1C6C50B"
    "046A739655BB7A5187226B200C2678E9D5B6EBB9485A599A6637CF01E039E55E50DF0C6F2B"
    "6B20AF418323FFF294898798BA8884F165842CF58C84657DBE4DAEAE2DB9F4724BEAB4F5C3"
    "755CC34F4E08901E2105BBB919A56CFB0F670203010001A38201DF308201DB30550603551D"
    "110101FF044B3049A447304531163014060567810502010C0B69643A343934363538303031"
    "173015060567810502020C0C534C42393634355454312E3231123010060567810502030C07"
    "69643A38353230300C0603551D130101FF040230003081BC0603551D200101FF0481B13081"
    "AE3081AB060B6086480186F84501072F0130819B303906082B06010505070201162D687474"
    "703A2F2F7777772E766572697369676E2E636F6D2F7265706F7369746F72792F696E646578"
    "2E68746D6C305E06082B0601050507020230521E5000540043005000410020005400720075"
    "007300740065006400200050006C006100740066006F0072006D0020004D006F0064007500"
    "6C006500200045006E0064006F007200730065006D0065006E0074301F0603551D23041830"
    "168014EC3F8D4CC12ABE88A019064E8A62B7018FA2E3593081930603551D0904818B308188"
    "303A06035504343133300B300906052B0E03021A05003024302206092A864886F70D010107"
    "3015A213301106092A864886F70D010109040454435041301606056781050210310D300B0C"
    "03312E32020102020103303206056781050212312930270101FFA0030A0101A1030A0100A2"
    "030A0100A310300E1603332E310A01040A01010101FF0101FF300D06092A864886F70D0101"
    "05050003820101003FC27BF941CEBB07E627CCE83DA5F08F5E0CFD0B87862CFD6560012B59"
    "F738DBFE76F32190B02EA7A63FA678D8CF06736A6836A2FA70F2DC6185176A80F975823028"
    "EB97C5C4881E7C7C215F2AFC7A179A9A02A373017016D8C7C48087F3555D712DF416723369"
    "1443FE8B81DDB6FEE69A49D3805DA89BA1477E892A4041B62B6BAC1EEEC280D53C80B346AB"
    "80EF8CC6D10E4D1C9AD1A5805E830F89A2FD5C87EE140F053B04E43757549F6A26A580D1D3"
    "17AED00C925FA93328D245772D0B2C74A933C813020EBC558C9F1381BF9F36E1350C6F1A1B"
    "9F7D5630698DBA6F0E2F334D101ACBB20E60963B12FCF61B61A2F76BC6AED4FC0F907D248B"
    "542F28BCB4";

}  // namespace

using BackendRoDataTpm1Test = BackendTpm1TestBase;

TEST_F(BackendRoDataTpm1Test, IsReady) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(315);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_PERSISTENT_WRITE_LOCK);
  info_reply.add_attributes(NvramSpaceAttribute::NVRAM_READ_AUTHORIZATION);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm1().IsReady(RoSpace::kEndorsementRsaCert),
              IsOkAndHolds(true));
}

TEST_F(BackendRoDataTpm1Test, IsReadyNotAvailable) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  info_reply.set_size(315);
  info_reply.set_is_read_locked(false);
  info_reply.set_is_write_locked(false);
  // Missing required attributes.
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm1().IsReady(RoSpace::kEndorsementRsaCert),
              IsOkAndHolds(false));
}

TEST_F(BackendRoDataTpm1Test, IsReadySpaceNotExist) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_SPACE_DOES_NOT_EXIST);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm1().IsReady(RoSpace::kEndorsementRsaCert),
              IsOkAndHolds(false));
}

TEST_F(BackendRoDataTpm1Test, IsReadyOtherError) {
  tpm_manager::GetSpaceInfoReply info_reply;
  info_reply.set_result(NvramResult::NVRAM_RESULT_DEVICE_ERROR);
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), GetSpaceInfo(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(info_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm1().IsReady(RoSpace::kEndorsementRsaCert),
              NotOk());
}

TEST_F(BackendRoDataTpm1Test, ReadRsaEk) {
  brillo::Blob fake_stored_cert;
  base::HexStringToBytes(kFakeStoredCert, &fake_stored_cert);
  brillo::Blob fake_extracted_cert;
  base::HexStringToBytes(kFakeExtractedCert, &fake_extracted_cert);

  tpm_manager::ReadSpaceReply read_reply;
  read_reply.set_result(NvramResult::NVRAM_RESULT_SUCCESS);
  read_reply.set_data(BlobToString(fake_stored_cert));
  EXPECT_CALL(proxy_->GetMockTpmNvramProxy(), ReadSpace(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<1>(read_reply), Return(true)));

  EXPECT_THAT(backend_->GetRoDataTpm1().Read(RoSpace::kEndorsementRsaCert),
              IsOkAndHolds(fake_extracted_cert));
}

}  // namespace hwsec
