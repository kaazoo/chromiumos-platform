// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_ENUMS_H_
#define LORGNETTE_ENUMS_H_

#include <string>

// Must agree with DocumentScanSaneBackend in chrome's enums.xml
enum DocumentScanSaneBackend {
  kOtherBackend = 0,
  kAbaton = 1,
  kAgfafocus = 2,
  kAirscanBrother = 3,
  kAirscanCanon = 4,
  kAirscanEpson = 5,
  kAirscanHp = 6,
  kAirscanKodak = 7,
  kAirscanKonicaMinolta = 8,
  kAirscanKyocera = 9,
  kAirscanLexmark = 10,
  kAirscanOther = 11,
  kAirscanRicoh = 12,
  kAirscanSamsung = 13,
  kAirscanXerox = 14,
  kApple = 15,
  kArtec = 16,
  kArtecEplus48U = 17,
  kAs6E = 18,
  kAvision = 19,
  kBh = 20,
  kCanon = 21,
  kCanon630U = 22,
  kCanonDr = 23,
  kCardscan = 24,
  kCoolscan = 25,
  kCoolscan2 = 26,
  kCoolscan3 = 27,
  kDc210 = 28,
  kDc240 = 29,
  kDc25 = 30,
  kDell1600NNet = 31,
  kDmc = 32,
  kEpjitsu = 33,
  kEpson = 34,
  kEpson2 = 35,
  kEscl = 36,
  kFujitsu = 37,
  kGenesys = 38,
  kGt68Xx = 39,
  kHp = 40,
  kHp3500 = 41,
  kHp3900 = 42,
  kHp4200 = 43,
  kHp5400 = 44,
  kHp5590 = 45,
  kHpljm1005 = 46,
  kHs2P = 47,
  kIbm = 48,
  kKodak = 49,
  kKodakaio = 50,
  kKvs1025 = 51,
  kKvs20Xx = 52,
  kKvs40Xx = 53,
  kLeo = 54,
  kLexmark = 55,
  kMa1509 = 56,
  kMagicolor = 57,
  kMatsushita = 58,
  kMicrotek = 59,
  kMicrotek2 = 60,
  kMustek = 61,
  kMustekUsb = 62,
  kMustekUsb2 = 63,
  kNec = 64,
  kNet = 65,
  kNiash = 66,
  kP5 = 67,
  kPie = 68,
  kPixma = 69,
  kPlustek = 70,
  kPlustekPp = 71,
  kQcam = 72,
  kRicoh = 73,
  kRicoh2 = 74,
  kRts8891 = 75,
  kS9036 = 76,
  kSceptre = 77,
  kSharp = 78,
  kSm3600 = 79,
  kSm3840 = 80,
  kSnapscan = 81,
  kSp15C = 82,
  kSt400 = 83,
  kStv680 = 84,
  kTamarack = 85,
  kTeco1 = 86,
  kTeco2 = 87,
  kTeco3 = 88,
  kTest = 89,
  kU12 = 90,
  kUmax = 91,
  kUmax1220U = 92,
  kUmaxPp = 93,
  kXeroxMfp = 94,
  kIppUsbBrother = 95,  // IppUsb is the airscan backend used over USB.
  kIppUsbCanon = 96,
  kIppUsbEpson = 97,
  kIppUsbHp = 98,
  kIppUsbKodak = 99,
  kIppUsbKonicaMinolta = 100,
  kIppUsbKyocera = 101,
  kIppUsbLexmark = 102,
  kIppUsbOther = 103,
  kIppUsbRicoh = 104,
  kIppUsbSamsung = 105,
  kIppUsbXerox = 106,
  kCanonLide70 = 107,
  kEpsonDs = 108,
  kP208ii = 109,
  kDrm260 = 110,
  kDrp208ii = 111,
  kP215ii = 112,
  kDrp215ii = 113,
  kDrc225ii = 114,
  kDrc230 = 115,
  kDrc240 = 116,
  kR40 = 117,
  kR50 = 118,
  kPfufs = 119,

  kMaxValue = kPfufs,
};

// Gets the UMA enum corresponding to the SANE backend with the given
// device_name. If no matching backend is found, returns kOtherBackend.
DocumentScanSaneBackend BackendFromDeviceName(const std::string& device);

#endif  // LORGNETTE_ENUMS_H_
