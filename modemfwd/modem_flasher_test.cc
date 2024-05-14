// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_flasher.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <chromeos/switches/modemfwd_switches.h>
#include <dbus/modemfwd/dbus-constants.h>
#include <gtest/gtest.h>

#include "modemfwd/firmware_directory_stub.h"
#include "modemfwd/mock_daemon_delegate.h"
#include "modemfwd/mock_journal.h"
#include "modemfwd/mock_metrics.h"
#include "modemfwd/mock_modem.h"
#include "modemfwd/mock_notification_manager.h"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::Mock;
using ::testing::Return;

namespace modemfwd {

namespace {

constexpr char kDeviceId1[] = "device:id:1";
constexpr char kEquipmentId1[] = "equipment_id_1";

constexpr char kMainFirmware1Path[] = "main_fw_1.fls";
constexpr char kMainFirmware1Version[] = "versionA";

constexpr char kMainFirmware2Path[] = "main_fw_2.fls";
constexpr char kMainFirmware2Version[] = "versionB";

constexpr char kOemFirmware1Path[] = "oem_cust_1.fls";
constexpr char kOemFirmware1Version[] = "6000.1";

constexpr char kOemFirmware2Path[] = "oem_cust_2.fls";
constexpr char kOemFirmware2Version[] = "6000.2";

constexpr char kCarrier1[] = "uuid_1";
constexpr char kCarrier1Mvno[] = "uuid_1_1";
constexpr char kCarrier1Firmware1Path[] = "carrier_1_fw_1.fls";
constexpr char kCarrier1Firmware1Version[] = "v1.00";
constexpr char kCarrier1Firmware2Path[] = "carrier_1_fw_2.fls";
constexpr char kCarrier1Firmware2Version[] = "v1.10";

constexpr char kCarrier2[] = "uuid_2";
constexpr char kCarrier2Firmware1Path[] = "carrier_2_fw_1.fls";
constexpr char kCarrier2Firmware1Version[] = "4500.15.65";

constexpr char kGenericCarrierFirmware1Path[] = "generic_fw_1.fls";
constexpr char kGenericCarrierFirmware1Version[] = "2017-10-13";
constexpr char kGenericCarrierFirmware2Path[] = "generic_fw_2.fls";
constexpr char kGenericCarrierFirmware2Version[] = "2017-10-14";

// Associated payloads
constexpr char kApFirmwareTag[] = "ap";
constexpr char kApFirmware1Path[] = "ap_firmware";
constexpr char kApFirmware1Version[] = "abc.a40";

constexpr char kApFirmware2Path[] = "ap_firmware_2";
constexpr char kApFirmware2Version[] = "def.g50";

constexpr char kDevFirmwareTag[] = "dev";
constexpr char kDevFirmwarePath[] = "dev_firmware";
constexpr char kDevFirmwareVersion[] = "000.012";

// Journal entry ID
constexpr char kJournalEntryId[] = "journal-entry";

}  // namespace

class ModemFlasherTest : public ::testing::Test {
 public:
  ModemFlasherTest() {
    firmware_directory_ =
        std::make_unique<FirmwareDirectoryStub>(base::FilePath());

    delegate_ = std::make_unique<MockDelegate>();
    journal_ = std::make_unique<MockJournal>();
    notification_mgr_ = std::make_unique<MockNotificationManager>();
    mock_metrics_ = std::make_unique<MockMetrics>();
    modem_flasher_ = std::make_unique<ModemFlasher>(
        delegate_.get(), firmware_directory_.get(), journal_.get(),
        notification_mgr_.get(), mock_metrics_.get());

    only_main_ = {kFwMain};
    only_carrier_ = {kFwCarrier};
  }

 protected:
  void AddMainFirmwareFile(const std::string& device_id,
                           const base::FilePath& rel_firmware_path,
                           const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddMainFirmware(kDeviceId1, firmware_info);
  }

  void AddAssocFirmwareFile(const std::string& main_fw_path,
                            const std::string& firmware_id,
                            const base::FilePath& rel_firmware_path,
                            const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddAssocFirmware(main_fw_path, firmware_id,
                                          firmware_info);
  }

  void AddMainFirmwareFileForCarrier(const std::string& device_id,
                                     const std::string& carrier_name,
                                     const base::FilePath& rel_firmware_path,
                                     const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddMainFirmwareForCarrier(kDeviceId1, carrier_name,
                                                   firmware_info);
  }

  void AddOemFirmwareFile(const std::string& device_id,
                          const base::FilePath& rel_firmware_path,
                          const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddOemFirmware(kDeviceId1, firmware_info);
  }

  void AddOemFirmwareFileForCarrier(const std::string& device_id,
                                    const std::string& carrier_name,
                                    const base::FilePath& rel_firmware_path,
                                    const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddOemFirmwareForCarrier(kDeviceId1, carrier_name,
                                                  firmware_info);
  }

  void AddCarrierFirmwareFile(const std::string& device_id,
                              const std::string& carrier_name,
                              const base::FilePath& rel_firmware_path,
                              const std::string& version) {
    FirmwareFileInfo firmware_info(rel_firmware_path.value(), version);
    firmware_directory_->AddCarrierFirmware(kDeviceId1, carrier_name,
                                            firmware_info);
  }

  std::unique_ptr<MockModem> GetDefaultModem() {
    auto modem = std::make_unique<MockModem>();
    ON_CALL(*modem, GetDeviceId()).WillByDefault(Return(kDeviceId1));
    ON_CALL(*modem, GetEquipmentId()).WillByDefault(Return(kEquipmentId1));
    ON_CALL(*modem, GetCarrierId()).WillByDefault(Return(kCarrier1));
    ON_CALL(*modem, GetMainFirmwareVersion())
        .WillByDefault(Return(kMainFirmware1Version));
    ON_CALL(*modem, GetOemFirmwareVersion())
        .WillByDefault(Return(kOemFirmware1Version));
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(""));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(""));

    // Since the equipment ID is the main identifier we should always expect
    // to want to know what it is.
    EXPECT_CALL(*modem, GetEquipmentId()).Times(AtLeast(1));
    return modem;
  }

  void SetCarrierFirmwareInfo(MockModem* modem,
                              const std::string& carrier_id,
                              const std::string& version) {
    ON_CALL(*modem, GetCarrierFirmwareId()).WillByDefault(Return(carrier_id));
    ON_CALL(*modem, GetCarrierFirmwareVersion()).WillByDefault(Return(version));
  }

  brillo::ErrorPtr err;
  std::unique_ptr<MockDelegate> delegate_;
  std::unique_ptr<MockJournal> journal_;
  std::unique_ptr<ModemFlasher> modem_flasher_;
  std::unique_ptr<MockNotificationManager> notification_mgr_;
  std::unique_ptr<MockMetrics> mock_metrics_;
  // helpers for the mock_journal calls
  std::vector<std::string> only_main_;
  std::vector<std::string> only_carrier_;

 private:
  std::unique_ptr<FirmwareDirectoryStub> firmware_directory_;
};

TEST_F(ModemFlasherTest, NewModemIsFlashable) {
  auto modem = GetDefaultModem();
  EXPECT_TRUE(modem_flasher_->ShouldFlash(modem.get(), &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NothingToFlash) {
  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromEmptyFirmwareDirectory) {
  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, FlashMainFirmware) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*mock_metrics_, SendFwFlashTime(_)).Times(1);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NewMainFirmwareAvailable) {
  const base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);
  const std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_EQ(cfg->fw_configs, main_cfg);
  ASSERT_EQ(cfg->files[kFwMain]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, FlashMainFirmwareEmptyCarrier) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  ON_CALL(*modem, GetCarrierId()).WillByDefault(Return(""));

  // Flash the main fw even when the carrier is unknown
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SkipSameMainVersion) {
  base::FilePath firmware(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, firmware, kMainFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameMainFirmware) {
  const base::FilePath firmware(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, firmware, kMainFirmware1Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SkipSameOemVersion) {
  base::FilePath firmware(kOemFirmware1Path);
  AddOemFirmwareFile(kDeviceId1, firmware, kOemFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetOemFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, UpgradeOemFirmware) {
  base::FilePath new_firmware(kOemFirmware2Path);
  AddOemFirmwareFile(kDeviceId1, new_firmware, kOemFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> oem_cfg = {
      {kFwOem, new_firmware, kOemFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetOemFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(oem_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*mock_metrics_, SendFwFlashTime(_)).Times(1);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NewOemFirmwareAvailable) {
  const base::FilePath new_firmware(kOemFirmware2Path);
  AddOemFirmwareFile(kDeviceId1, new_firmware, kOemFirmware2Version);
  std::vector<FirmwareConfig> oem_cfg = {
      {kFwOem, new_firmware, kOemFirmware2Version}};

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, oem_cfg);
  ASSERT_EQ(cfg->files[kFwOem]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameOemFirmware) {
  const base::FilePath firmware(kOemFirmware1Path);
  AddOemFirmwareFile(kDeviceId1, firmware, kOemFirmware1Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, UpgradeCarrierFirmware) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, NewCarrierFirmwareAvailable) {
  const base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_EQ(cfg->fw_configs, carrier_cfg);
  ASSERT_EQ(cfg->files[kFwCarrier]->path_on_filesystem(), new_firmware);
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, EmptyConfigFromSameCarrierFirmware) {
  const base::FilePath original_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_firmware,
                         kCarrier1Firmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);

  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_TRUE(cfg->fw_configs.empty());
  ASSERT_TRUE(cfg->files.empty());
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SwitchCarrierFirmwareForSimHotSwap) {
  base::FilePath original_firmware(kCarrier1Firmware1Path);
  base::FilePath other_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_firmware,
                         kCarrier1Firmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_firmware,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_other_cfg = {
      {kFwCarrier, other_firmware, kCarrier2Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_other_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // After the modem reboots, the helper hopefully reports the new carrier.
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Suppose we swap the SIM back to the first one. Then we should try to
  // flash the first firmware again.
  modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_orig_cfg = {
      {kFwCarrier, original_firmware, kCarrier1Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_orig_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, BlockAfterMainFlashFailure) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillRepeatedly(Return(false));
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);

  // ModemFlasher retries once on a failure, so fail twice.
  modem = GetDefaultModem();
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);

  // Here the modem would reboot, but ModemFlasher should keep track of its
  // IMEI and ensure we don't even check the main firmware version or
  // carrier.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(0);
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, GetCarrierId()).Times(0);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ShouldNotFlashAfterMainFlashFailure) {
  const base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);
  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_NE(cfg, nullptr);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillRepeatedly(Return(false));
  // The first flash failure should not block the modem.
  ASSERT_FALSE(
      modem_flasher_->RunFlash(modem.get(), *cfg, true, nullptr, &err));
  ASSERT_TRUE(modem_flasher_->ShouldFlash(modem.get(), &err));
  // The second one will.
  ASSERT_FALSE(
      modem_flasher_->RunFlash(modem.get(), *cfg, true, nullptr, &err));
  ASSERT_FALSE(modem_flasher_->ShouldFlash(modem.get(), &err));
}

TEST_F(ModemFlasherTest, BlockAfterCarrierFlashFailure) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg))
      .WillRepeatedly(Return(false));
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);

  // ModemFlasher retries once on a failure, so fail twice.
  modem = GetDefaultModem();
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);

  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(0);
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, GetCarrierId()).Times(0);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ShouldNotFlashAfterCarrierFlashFailure) {
  const base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);
  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_NE(cfg, nullptr);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillRepeatedly(Return(false));
  // The first flash failure should not block the modem.
  ASSERT_FALSE(
      modem_flasher_->RunFlash(modem.get(), *cfg, true, nullptr, &err));
  ASSERT_TRUE(modem_flasher_->ShouldFlash(modem.get(), &err));
  // The second one will.
  ASSERT_FALSE(
      modem_flasher_->RunFlash(modem.get(), *cfg, true, nullptr, &err));
  ASSERT_FALSE(modem_flasher_->ShouldFlash(modem.get(), &err));
}

TEST_F(ModemFlasherTest, RefuseToFlashMainFirmwareTwice) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // We've had issues in the past where the firmware version is updated
  // but the modem still reports the old version string. Refuse to flash
  // the main firmware twice because that should never be correct behavior
  // in one session. Otherwise, we might try to flash the main firmware
  // over and over.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, RefuseToFlashOemFirmwareTwice) {
  base::FilePath new_firmware(kOemFirmware2Path);
  AddOemFirmwareFile(kDeviceId1, new_firmware, kOemFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> oem_cfg = {
      {kFwOem, new_firmware, kOemFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetOemFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(oem_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Assume that the modem fails to return properly the new version.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetOemFirmwareVersion()).Times(0);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, RefuseToFlashCarrierFirmwareTwice) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Assume the carrier firmware doesn't have an updated version string in it,
  // i.e. the modem will return the old version string even if it's been
  // updated.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, RefuseToReflashCarrierAcrossHotSwap) {
  // Upgrade carrier firmware.
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Switch carriers, but there won't be firmware for the new one.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware2Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Suppose we swap the SIM back to the first one. We should not flash
  // firmware that we already know we successfully flashed.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware2Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, UpgradeGenericFirmware) {
  base::FilePath new_firmware(kGenericCarrierFirmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         new_firmware, kGenericCarrierFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kGenericCarrierFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SkipSameGenericFirmware) {
  base::FilePath generic_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         generic_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, TwoCarriersUsingGenericFirmware) {
  base::FilePath generic_firmware(kGenericCarrierFirmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         generic_firmware, kGenericCarrierFirmware1Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, generic_firmware, kGenericCarrierFirmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // When we try to flash again and the modem reports a different carrier,
  // we should expect that the ModemFlasher refuses to flash the same firmware,
  // since there is generic firmware and no carrier has its own firmware.
  modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, HotSwapWithGenericFirmware) {
  base::FilePath original_firmware(kGenericCarrierFirmware1Path);
  base::FilePath other_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, FirmwareDirectory::kGenericCarrierId,
                         original_firmware, kGenericCarrierFirmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_firmware,
                         kCarrier2Firmware1Version);

  // Even though there is generic firmware, we should try to use specific
  // ones first if they exist.
  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_other_cfg = {
      {kFwCarrier, other_firmware, kCarrier2Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), FirmwareDirectory::kGenericCarrierId,
                         kGenericCarrierFirmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_other_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Reboot the modem.
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Suppose we swap the SIM back to the first one. Then we should try to
  // flash the generic firmware again.
  modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_orig_cfg = {
      {kFwCarrier, original_firmware, kGenericCarrierFirmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_orig_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, WritesToJournal) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(only_main_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);

  // The cleanup callback marks the end of flashing the firmware.
  base::OnceClosure cb;
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce([&cb](const std::string& /*equipment_id*/,
                      base::OnceClosure reg_cb) { cb = std::move(reg_cb); });
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
  ASSERT_FALSE(cb.is_null());
  std::move(cb).Run();
}

TEST_F(ModemFlasherTest, WritesToJournalOnFailure) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(false));
  EXPECT_CALL(*journal_, MarkStartOfFlashingFirmware(only_main_, kDeviceId1, _))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);
  // There should be no cleanup callback after the flashing fails, as it is done
  // synchronously with the failure.
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _)).Times(0);
  ASSERT_FALSE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_NE(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, WritesCarrierSwitchesToJournal) {
  base::FilePath original_firmware(kCarrier1Firmware1Path);
  base::FilePath other_firmware(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_firmware,
                         kCarrier1Firmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_firmware,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_other_cfg = {
      {kFwCarrier, other_firmware, kCarrier2Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_other_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*journal_,
              MarkStartOfFlashingFirmware(only_carrier_, kDeviceId1, kCarrier2))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);

  base::OnceClosure cb;

  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce([&cb](const std::string& /*equipment_id*/,
                      base::OnceClosure reg_cb) { cb = std::move(reg_cb); });
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
  ASSERT_FALSE(cb.is_null());
  std::move(cb).Run();

  Mock::VerifyAndClearExpectations(delegate_.get());

  // After the modem reboots, the helper hopefully reports the new carrier.
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  Mock::VerifyAndClearExpectations(delegate_.get());

  // Suppose we swap the SIM back to the first one. Then we should try to
  // flash the first firmware again.
  modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_orig_cfg = {
      {kFwCarrier, original_firmware, kCarrier1Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_orig_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*journal_,
              MarkStartOfFlashingFirmware(only_carrier_, kDeviceId1, kCarrier1))
      .WillOnce(Return(kJournalEntryId));
  EXPECT_CALL(*journal_, MarkEndOfFlashingFirmware(kJournalEntryId)).Times(1);
  EXPECT_CALL(*delegate_, RegisterOnModemReappearanceCallback(_, _))
      .WillOnce([&cb](const std::string& /*equipment_id*/,
                      base::OnceClosure reg_cb) { cb = std::move(reg_cb); });
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
  ASSERT_FALSE(cb.is_null());
  std::move(cb).Run();
}

TEST_F(ModemFlasherTest, CarrierSwitchingMainFirmware) {
  base::FilePath original_main(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, original_main, kMainFirmware1Version);
  base::FilePath other_main(kMainFirmware2Path);
  AddMainFirmwareFileForCarrier(kDeviceId1, kCarrier2, other_main,
                                kMainFirmware2Version);

  base::FilePath original_oem(kOemFirmware1Path);
  AddOemFirmwareFile(kDeviceId1, original_oem, kOemFirmware1Version);
  base::FilePath other_oem(kOemFirmware2Path);
  AddOemFirmwareFileForCarrier(kDeviceId1, kCarrier2, other_oem,
                               kOemFirmware2Version);

  base::FilePath original_carrier(kCarrier1Firmware1Path);
  base::FilePath other_carrier(kCarrier2Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, original_carrier,
                         kCarrier1Firmware1Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier2, other_carrier,
                         kCarrier2Firmware1Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> other_cfg = {
      {kFwMain, other_main, kMainFirmware2Version},
      {kFwOem, other_oem, kOemFirmware2Version},
      {kFwCarrier, other_carrier, kCarrier2Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier2));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(other_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);

  // Switch the carrier back and make sure we flash all firmware blobs
  // again.
  modem = GetDefaultModem();
  std::vector<FirmwareConfig> orig_cfg = {
      {kFwMain, original_main, kMainFirmware1Version},
      {kFwOem, original_oem, kOemFirmware1Version},
      {kFwCarrier, original_carrier, kCarrier1Firmware1Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kMainFirmware2Version));
  EXPECT_CALL(*modem, GetOemFirmwareVersion())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kOemFirmware2Version));
  EXPECT_CALL(*modem, GetCarrierId())
      .Times(AtLeast(1))
      .WillRepeatedly(Return(kCarrier1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier2, kCarrier2Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(orig_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, InhibitDuringMainFirmwareFlash) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, new_firmware, kMainFirmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(true)).WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(false)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, InhibitDuringCarrierFirmwareFlash) {
  base::FilePath new_firmware(kCarrier1Firmware2Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, new_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> carrier_cfg = {
      {kFwCarrier, new_firmware, kCarrier1Firmware2Version}};
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  SetCarrierFirmwareInfo(modem.get(), kCarrier1, kCarrier1Firmware1Version);
  EXPECT_CALL(*modem, FlashFirmwares(carrier_cfg)).WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(true)).WillOnce(Return(true));
  EXPECT_CALL(*modem, SetInhibited(false)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, SkipCarrierWithTwoUuidSameFirmware) {
  base::FilePath current_firmware(kCarrier1Firmware1Path);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1, current_firmware,
                         kCarrier1Firmware2Version);
  AddCarrierFirmwareFile(kDeviceId1, kCarrier1Mvno, current_firmware,
                         kCarrier1Firmware2Version);

  auto modem = GetDefaultModem();
  EXPECT_CALL(*modem, GetDeviceId()).Times(AtLeast(1));
  EXPECT_CALL(*modem, GetCarrierFirmwareVersion()).Times(AtLeast(1));
  // The modem will say that the currently flashed firmware has the carrier UUID
  // KCarrier1Mvno while the current carrier UUID is always returned as
  // kCarrier1.
  SetCarrierFirmwareInfo(modem.get(), kCarrier1Mvno, kCarrier1Firmware2Version);
  EXPECT_CALL(*modem, FlashFirmwares(_)).Times(0);
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, FlashAssociatedFirmware) {
  const base::FilePath main_fw_path(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, main_fw_path, kMainFirmware2Version);
  const base::FilePath ap_fw_path(kApFirmware1Path);
  AddAssocFirmwareFile(kMainFirmware2Path, kApFirmwareTag, ap_fw_path,
                       kApFirmware1Version);
  const base::FilePath dev_fw_path(kDevFirmwarePath);
  AddAssocFirmwareFile(kMainFirmware2Path, kDevFirmwareTag, dev_fw_path,
                       kDevFirmwareVersion);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> main_cfg = {
      {kFwMain, main_fw_path, kMainFirmware2Version},
      {kApFirmwareTag, ap_fw_path, kApFirmware1Version},
      {kDevFirmwareTag, dev_fw_path, kDevFirmwareVersion}};
  EXPECT_CALL(*modem, FlashFirmwares(main_cfg)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ConfigHasAssocFirmware) {
  const base::FilePath main_fw_path(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, main_fw_path, kMainFirmware2Version);
  const base::FilePath ap_fw_path(kApFirmware1Path);
  AddAssocFirmwareFile(kMainFirmware2Path, kApFirmwareTag, ap_fw_path,
                       kApFirmware1Version);
  const base::FilePath dev_fw_path(kDevFirmwarePath);
  AddAssocFirmwareFile(kMainFirmware2Path, kDevFirmwareTag, dev_fw_path,
                       kDevFirmwareVersion);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);

  ASSERT_EQ(std::ranges::count(
                cfg->fw_configs,
                FirmwareConfig{kFwMain, main_fw_path, kMainFirmware2Version}),
            1);
  ASSERT_EQ(std::ranges::count(cfg->fw_configs,
                               FirmwareConfig{kApFirmwareTag, ap_fw_path,
                                              kApFirmware1Version}),
            1);
  ASSERT_EQ(std::ranges::count(cfg->fw_configs,
                               FirmwareConfig{kDevFirmwareTag, dev_fw_path,
                                              kDevFirmwareVersion}),
            1);

  ASSERT_EQ(cfg->files[kFwMain]->path_on_filesystem(), main_fw_path);
  ASSERT_EQ(cfg->files[kApFirmwareTag]->path_on_filesystem(), ap_fw_path);
  ASSERT_EQ(cfg->files[kDevFirmwareTag]->path_on_filesystem(), dev_fw_path);

  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, UpgradeAssocFirmwareOnly) {
  const base::FilePath main_fw_path(kMainFirmware1Path);
  AddMainFirmwareFile(kDeviceId1, main_fw_path, kMainFirmware1Version);
  const base::FilePath ap_fw_path(kApFirmware2Path);
  AddAssocFirmwareFile(kMainFirmware1Path, kApFirmwareTag, ap_fw_path,
                       kApFirmware2Version);

  auto modem = GetDefaultModem();
  std::vector<FirmwareConfig> config = {
      {kApFirmwareTag, ap_fw_path, kApFirmware2Version}};
  EXPECT_CALL(*modem, GetMainFirmwareVersion()).Times(AtLeast(1));
  EXPECT_CALL(*modem, FlashFirmwares(config)).WillOnce(Return(true));
  ASSERT_TRUE(modem_flasher_->TryFlash(modem.get(), true, &err));
  ASSERT_EQ(err.get(), nullptr);
}

TEST_F(ModemFlasherTest, ModemNeverSeenError) {
  base::FilePath new_firmware(kMainFirmware2Path);
  AddMainFirmwareFile(kDeviceId1, new_firmware, kMainFirmware2Version);

  auto modem = GetDefaultModem();
  auto cfg = modem_flasher_->BuildFlashConfig(modem.get(), &err);
  ASSERT_NE(cfg, nullptr);

  EXPECT_CALL(*modem, FlashFirmwares(_)).WillRepeatedly(Return(false));

  ASSERT_FALSE(
      modem_flasher_->RunFlash(modem.get(), *cfg, true, nullptr, &err));
  ASSERT_NE(err.get(), nullptr);
  ASSERT_EQ(err.get()->GetCode(), kErrorResultFailureReturnedByHelper);

  ASSERT_FALSE(
      modem_flasher_->RunFlash(modem.get(), *cfg, false, nullptr, &err));
  ASSERT_NE(err.get(), nullptr);
  ASSERT_EQ(err.get()->GetCode(),
            kErrorResultFailureReturnedByHelperModemNeverSeen);
}

}  // namespace modemfwd
