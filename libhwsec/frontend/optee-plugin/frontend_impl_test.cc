// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <memory>
#include <utility>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>

#include "libhwsec/backend/mock_backend.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/factory/tpm2_simulator_factory_for_test.h"
#include "libhwsec/structures/space.h"

using brillo::BlobFromString;
using hwsec_foundation::error::testing::IsOk;
using hwsec_foundation::error::testing::IsOkAndHolds;
using hwsec_foundation::error::testing::NotOk;
using hwsec_foundation::error::testing::NotOkAnd;
using hwsec_foundation::error::testing::NotOkWith;
using hwsec_foundation::error::testing::ReturnError;
using hwsec_foundation::error::testing::ReturnValue;
using testing::_;
using testing::AtMost;

namespace {

constexpr uint8_t kFakeCikCert[] = {
    0x30, 0x82, 0x01, 0xe8, 0x30, 0x82, 0x01, 0x8d, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x0a, 0x49, 0x68, 0xf6, 0x2e, 0xf6, 0xa4, 0xea, 0x51, 0xa2,
    0x0b, 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03,
    0x02, 0x30, 0x64, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06,
    0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04,
    0x08, 0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69,
    0x61, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0b,
    0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31,
    0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0b, 0x45, 0x6e,
    0x67, 0x69, 0x6e, 0x65, 0x65, 0x72, 0x69, 0x6e, 0x67, 0x31, 0x14, 0x30,
    0x12, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0b, 0x43, 0x52, 0x4f, 0x53,
    0x20, 0x44, 0x32, 0x20, 0x43, 0x49, 0x4b, 0x30, 0x1e, 0x17, 0x0d, 0x32,
    0x30, 0x30, 0x37, 0x30, 0x38, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a,
    0x17, 0x0d, 0x34, 0x39, 0x31, 0x32, 0x33, 0x31, 0x32, 0x33, 0x35, 0x39,
    0x35, 0x39, 0x5a, 0x30, 0x68, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55,
    0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03,
    0x55, 0x04, 0x08, 0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72,
    0x6e, 0x69, 0x61, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a,
    0x13, 0x0b, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63,
    0x2e, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0b,
    0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x65, 0x72, 0x69, 0x6e, 0x67, 0x31,
    0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0f, 0x43, 0x52,
    0x4f, 0x53, 0x20, 0x44, 0x32, 0x20, 0x54, 0x50, 0x4d, 0x20, 0x43, 0x49,
    0x4b, 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,
    0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07,
    0x03, 0x42, 0x00, 0x04, 0x64, 0x9e, 0x39, 0x2b, 0x75, 0xf3, 0x62, 0x0b,
    0x68, 0x4f, 0xf3, 0x5e, 0xc3, 0x2b, 0xeb, 0x2e, 0xb2, 0xff, 0xcb, 0x63,
    0x3f, 0x24, 0x8b, 0x4a, 0x4a, 0xc9, 0xeb, 0xf3, 0xa2, 0xfe, 0x41, 0x52,
    0xdf, 0x21, 0x7c, 0xbe, 0x0a, 0x06, 0xfa, 0xda, 0xb0, 0xab, 0xd2, 0x0d,
    0xa0, 0xc9, 0xe8, 0xa8, 0x2d, 0x8e, 0xf2, 0xba, 0xdd, 0x63, 0x09, 0xfd,
    0x5f, 0x4b, 0xe1, 0xd8, 0x5c, 0xca, 0x7d, 0xcd, 0xa3, 0x23, 0x30, 0x21,
    0x30, 0x1f, 0x06, 0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80,
    0x14, 0x9d, 0x78, 0x09, 0x49, 0x0b, 0xd9, 0x19, 0xbe, 0x2c, 0x69, 0x8f,
    0x53, 0x83, 0xf8, 0x34, 0x53, 0xb0, 0x4e, 0xe3, 0xf2, 0x30, 0x0a, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03, 0x49, 0x00,
    0x30, 0x46, 0x02, 0x21, 0x00, 0xe2, 0xf7, 0x1b, 0x21, 0x0f, 0xdf, 0xb1,
    0x95, 0xc6, 0x84, 0x28, 0xd4, 0x86, 0x3b, 0x3f, 0x45, 0xc1, 0x17, 0xdc,
    0xdc, 0x16, 0x41, 0x21, 0xaf, 0x53, 0xcd, 0x62, 0xb4, 0xb1, 0xd7, 0xb9,
    0x5f, 0x02, 0x21, 0x00, 0x80, 0x1b, 0x59, 0xb3, 0xe7, 0x4d, 0x6e, 0x79,
    0xd7, 0x9f, 0xa1, 0xb7, 0xff, 0xda, 0xbf, 0x13, 0x13, 0x85, 0xa8, 0x3e,
    0xec, 0x2a, 0x43, 0xf6, 0x51, 0xbb, 0xac, 0xb2, 0xcb, 0x2e, 0x89, 0xe7,
};

constexpr uint8_t kFakeRotCert[] = {
    0x30, 0x82, 0x02, 0x4E, 0x30, 0x82, 0x01, 0xF4, 0xA0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x4C, 0xFD, 0x5B, 0x1B, 0x42, 0xEF, 0xFA, 0x30, 0x0A, 0x06, 0x08, 0x2A,
    0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02, 0x30, 0x68, 0x31, 0x0B, 0x30,
    0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13,
    0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0A, 0x43, 0x61, 0x6C,
    0x69, 0x66, 0x6F, 0x72, 0x6E, 0x69, 0x61, 0x31, 0x14, 0x30, 0x12, 0x06,
    0x03, 0x55, 0x04, 0x0A, 0x13, 0x0B, 0x47, 0x6F, 0x6F, 0x67, 0x6C, 0x65,
    0x20, 0x49, 0x6E, 0x63, 0x2E, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55,
    0x04, 0x0B, 0x13, 0x0B, 0x45, 0x6E, 0x67, 0x69, 0x6E, 0x65, 0x65, 0x72,
    0x69, 0x6E, 0x67, 0x31, 0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04, 0x03,
    0x13, 0x0F, 0x43, 0x52, 0x4F, 0x53, 0x20, 0x44, 0x32, 0x20, 0x54, 0x50,
    0x4D, 0x20, 0x43, 0x49, 0x4B, 0x30, 0x22, 0x18, 0x0F, 0x32, 0x30, 0x30,
    0x30, 0x30, 0x31, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5A,
    0x18, 0x0F, 0x32, 0x30, 0x39, 0x39, 0x31, 0x32, 0x33, 0x31, 0x32, 0x33,
    0x35, 0x39, 0x35, 0x39, 0x5A, 0x30, 0x0F, 0x31, 0x0D, 0x30, 0x0B, 0x06,
    0x03, 0x55, 0x04, 0x03, 0x0C, 0x04, 0x54, 0x69, 0x35, 0x30, 0x30, 0x59,
    0x30, 0x13, 0x06, 0x07, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06,
    0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
    0x04, 0x03, 0x4C, 0xFD, 0x5B, 0x1B, 0x42, 0xEF, 0xFA, 0x38, 0xD5, 0x74,
    0xCF, 0x9E, 0x04, 0x85, 0x43, 0xC5, 0xF5, 0xB2, 0x96, 0x3D, 0x22, 0xA7,
    0xE8, 0x8F, 0x7C, 0xA7, 0x6F, 0x2D, 0x6A, 0x32, 0xCE, 0xA0, 0x19, 0x9C,
    0xB7, 0x13, 0x05, 0xC6, 0xAE, 0xC8, 0xFA, 0x97, 0x45, 0xD7, 0x41, 0x02,
    0xDA, 0xA9, 0xDB, 0xEF, 0x3D, 0xA4, 0x5D, 0x4C, 0xCB, 0xFC, 0xF3, 0x0C,
    0x37, 0x6D, 0x9E, 0x77, 0xCC, 0xA3, 0x81, 0xD4, 0x30, 0x81, 0xD1, 0x30,
    0x1A, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01,
    0x21, 0x04, 0x0C, 0x5A, 0x53, 0x5A, 0x56, 0xA5, 0xAC, 0xA5, 0xA9, 0x7F,
    0x7F, 0x00, 0x00, 0x30, 0x0F, 0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01,
    0xD6, 0x79, 0x02, 0x01, 0x22, 0x04, 0x01, 0x14, 0x30, 0x2E, 0x06, 0x0A,
    0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01, 0x23, 0x04, 0x20,
    0x89, 0xEA, 0xF3, 0x51, 0x34, 0xB4, 0xB3, 0xC6, 0x49, 0xF4, 0x4C, 0x0C,
    0x76, 0x5B, 0x96, 0xAE, 0xAB, 0x8B, 0xB3, 0x4E, 0xE8, 0x3C, 0xC7, 0xA6,
    0x83, 0xC4, 0xE5, 0x3D, 0x15, 0x81, 0xC8, 0xC7, 0x30, 0x2E, 0x06, 0x0A,
    0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01, 0x24, 0x04, 0x20,
    0xDC, 0x54, 0xF2, 0x9F, 0x4B, 0xF4, 0xC7, 0x58, 0x4A, 0x8D, 0x99, 0x15,
    0xB0, 0x93, 0x59, 0x95, 0x9F, 0x8E, 0x13, 0xF1, 0x30, 0x09, 0xC1, 0xEA,
    0xB4, 0xB0, 0x0C, 0x21, 0xB8, 0x5F, 0x19, 0x30, 0x30, 0x2E, 0x06, 0x0A,
    0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01, 0x25, 0x04, 0x20,
    0xDC, 0x54, 0xF2, 0x9F, 0x4B, 0xF4, 0xC7, 0x58, 0x4A, 0x8D, 0x99, 0x15,
    0xB0, 0x93, 0x59, 0x95, 0x9F, 0x8E, 0x13, 0xF1, 0x30, 0x09, 0xC1, 0xEA,
    0xB4, 0xB0, 0x0C, 0x21, 0xB8, 0x5F, 0x19, 0x30, 0x30, 0x12, 0x06, 0x0A,
    0x2B, 0x06, 0x01, 0x04, 0x01, 0xD6, 0x79, 0x02, 0x01, 0x26, 0x04, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x30, 0x0A, 0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE,
    0x3D, 0x04, 0x03, 0x02, 0x03, 0x48, 0x00, 0x30, 0x45, 0x02, 0x21, 0x00,
    0xA1, 0x69, 0x90, 0x27, 0xA8, 0x5B, 0x4B, 0x2E, 0xAD, 0xC4, 0x2B, 0xC4,
    0x21, 0x03, 0x45, 0xE5, 0x34, 0xA9, 0x8E, 0x2D, 0x6B, 0x99, 0x32, 0x13,
    0xA6, 0x36, 0xFB, 0xF5, 0xD1, 0xBC, 0x89, 0x81, 0x02, 0x20, 0x60, 0x1B,
    0x9E, 0x7A, 0x03, 0x97, 0x8C, 0x16, 0xDF, 0x34, 0x98, 0x81, 0x87, 0xDF,
    0x8E, 0xFA, 0xF2, 0x8C, 0x4A, 0xB9, 0x3B, 0x6B, 0x73, 0xAF, 0xFF, 0x01,
    0xEF, 0x31, 0x64, 0x0D, 0x16, 0xFF,
};

constexpr uint8_t kExpectedPkcs7[] = {
    0x30, 0x82, 0x04, 0x69, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
    0x01, 0x07, 0x02, 0xa0, 0x82, 0x04, 0x5a, 0x30, 0x82, 0x04, 0x56, 0x02,
    0x01, 0x01, 0x31, 0x00, 0x30, 0x0b, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
    0xf7, 0x0d, 0x01, 0x07, 0x01, 0xa0, 0x82, 0x04, 0x3e, 0x30, 0x82, 0x01,
    0xe8, 0x30, 0x82, 0x01, 0x8d, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x0a,
    0x49, 0x68, 0xf6, 0x2e, 0xf6, 0xa4, 0xea, 0x51, 0xa2, 0x0b, 0x30, 0x0a,
    0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x30, 0x64,
    0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x55,
    0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x0a,
    0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61, 0x31, 0x14,
    0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0b, 0x47, 0x6f, 0x6f,
    0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31, 0x14, 0x30, 0x12,
    0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0b, 0x45, 0x6e, 0x67, 0x69, 0x6e,
    0x65, 0x65, 0x72, 0x69, 0x6e, 0x67, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03,
    0x55, 0x04, 0x03, 0x13, 0x0b, 0x43, 0x52, 0x4f, 0x53, 0x20, 0x44, 0x32,
    0x20, 0x43, 0x49, 0x4b, 0x30, 0x1e, 0x17, 0x0d, 0x32, 0x30, 0x30, 0x37,
    0x30, 0x38, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a, 0x17, 0x0d, 0x34,
    0x39, 0x31, 0x32, 0x33, 0x31, 0x32, 0x33, 0x35, 0x39, 0x35, 0x39, 0x5a,
    0x30, 0x68, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13,
    0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x08,
    0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f, 0x72, 0x6e, 0x69, 0x61,
    0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x13, 0x0b, 0x47,
    0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e, 0x63, 0x2e, 0x31, 0x14,
    0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13, 0x0b, 0x45, 0x6e, 0x67,
    0x69, 0x6e, 0x65, 0x65, 0x72, 0x69, 0x6e, 0x67, 0x31, 0x18, 0x30, 0x16,
    0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0f, 0x43, 0x52, 0x4f, 0x53, 0x20,
    0x44, 0x32, 0x20, 0x54, 0x50, 0x4d, 0x20, 0x43, 0x49, 0x4b, 0x30, 0x59,
    0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00,
    0x04, 0x64, 0x9e, 0x39, 0x2b, 0x75, 0xf3, 0x62, 0x0b, 0x68, 0x4f, 0xf3,
    0x5e, 0xc3, 0x2b, 0xeb, 0x2e, 0xb2, 0xff, 0xcb, 0x63, 0x3f, 0x24, 0x8b,
    0x4a, 0x4a, 0xc9, 0xeb, 0xf3, 0xa2, 0xfe, 0x41, 0x52, 0xdf, 0x21, 0x7c,
    0xbe, 0x0a, 0x06, 0xfa, 0xda, 0xb0, 0xab, 0xd2, 0x0d, 0xa0, 0xc9, 0xe8,
    0xa8, 0x2d, 0x8e, 0xf2, 0xba, 0xdd, 0x63, 0x09, 0xfd, 0x5f, 0x4b, 0xe1,
    0xd8, 0x5c, 0xca, 0x7d, 0xcd, 0xa3, 0x23, 0x30, 0x21, 0x30, 0x1f, 0x06,
    0x03, 0x55, 0x1d, 0x23, 0x04, 0x18, 0x30, 0x16, 0x80, 0x14, 0x9d, 0x78,
    0x09, 0x49, 0x0b, 0xd9, 0x19, 0xbe, 0x2c, 0x69, 0x8f, 0x53, 0x83, 0xf8,
    0x34, 0x53, 0xb0, 0x4e, 0xe3, 0xf2, 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x04, 0x03, 0x02, 0x03, 0x49, 0x00, 0x30, 0x46, 0x02,
    0x21, 0x00, 0xe2, 0xf7, 0x1b, 0x21, 0x0f, 0xdf, 0xb1, 0x95, 0xc6, 0x84,
    0x28, 0xd4, 0x86, 0x3b, 0x3f, 0x45, 0xc1, 0x17, 0xdc, 0xdc, 0x16, 0x41,
    0x21, 0xaf, 0x53, 0xcd, 0x62, 0xb4, 0xb1, 0xd7, 0xb9, 0x5f, 0x02, 0x21,
    0x00, 0x80, 0x1b, 0x59, 0xb3, 0xe7, 0x4d, 0x6e, 0x79, 0xd7, 0x9f, 0xa1,
    0xb7, 0xff, 0xda, 0xbf, 0x13, 0x13, 0x85, 0xa8, 0x3e, 0xec, 0x2a, 0x43,
    0xf6, 0x51, 0xbb, 0xac, 0xb2, 0xcb, 0x2e, 0x89, 0xe7, 0x30, 0x82, 0x02,
    0x4e, 0x30, 0x82, 0x01, 0xf4, 0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x10,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x4c, 0xfd, 0x5b,
    0x1b, 0x42, 0xef, 0xfa, 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
    0x3d, 0x04, 0x03, 0x02, 0x30, 0x68, 0x31, 0x0b, 0x30, 0x09, 0x06, 0x03,
    0x55, 0x04, 0x06, 0x13, 0x02, 0x55, 0x53, 0x31, 0x13, 0x30, 0x11, 0x06,
    0x03, 0x55, 0x04, 0x08, 0x13, 0x0a, 0x43, 0x61, 0x6c, 0x69, 0x66, 0x6f,
    0x72, 0x6e, 0x69, 0x61, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04,
    0x0a, 0x13, 0x0b, 0x47, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x20, 0x49, 0x6e,
    0x63, 0x2e, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04, 0x0b, 0x13,
    0x0b, 0x45, 0x6e, 0x67, 0x69, 0x6e, 0x65, 0x65, 0x72, 0x69, 0x6e, 0x67,
    0x31, 0x18, 0x30, 0x16, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0f, 0x43,
    0x52, 0x4f, 0x53, 0x20, 0x44, 0x32, 0x20, 0x54, 0x50, 0x4d, 0x20, 0x43,
    0x49, 0x4b, 0x30, 0x22, 0x18, 0x0f, 0x32, 0x30, 0x30, 0x30, 0x30, 0x31,
    0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a, 0x18, 0x0f, 0x32,
    0x30, 0x39, 0x39, 0x31, 0x32, 0x33, 0x31, 0x32, 0x33, 0x35, 0x39, 0x35,
    0x39, 0x5a, 0x30, 0x0f, 0x31, 0x0d, 0x30, 0x0b, 0x06, 0x03, 0x55, 0x04,
    0x03, 0x0c, 0x04, 0x54, 0x69, 0x35, 0x30, 0x30, 0x59, 0x30, 0x13, 0x06,
    0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04, 0x03, 0x4c,
    0xfd, 0x5b, 0x1b, 0x42, 0xef, 0xfa, 0x38, 0xd5, 0x74, 0xcf, 0x9e, 0x04,
    0x85, 0x43, 0xc5, 0xf5, 0xb2, 0x96, 0x3d, 0x22, 0xa7, 0xe8, 0x8f, 0x7c,
    0xa7, 0x6f, 0x2d, 0x6a, 0x32, 0xce, 0xa0, 0x19, 0x9c, 0xb7, 0x13, 0x05,
    0xc6, 0xae, 0xc8, 0xfa, 0x97, 0x45, 0xd7, 0x41, 0x02, 0xda, 0xa9, 0xdb,
    0xef, 0x3d, 0xa4, 0x5d, 0x4c, 0xcb, 0xfc, 0xf3, 0x0c, 0x37, 0x6d, 0x9e,
    0x77, 0xcc, 0xa3, 0x81, 0xd4, 0x30, 0x81, 0xd1, 0x30, 0x1a, 0x06, 0x0a,
    0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x21, 0x04, 0x0c,
    0x5a, 0x53, 0x5a, 0x56, 0xa5, 0xac, 0xa5, 0xa9, 0x7f, 0x7f, 0x00, 0x00,
    0x30, 0x0f, 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02,
    0x01, 0x22, 0x04, 0x01, 0x14, 0x30, 0x2e, 0x06, 0x0a, 0x2b, 0x06, 0x01,
    0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x23, 0x04, 0x20, 0x89, 0xea, 0xf3,
    0x51, 0x34, 0xb4, 0xb3, 0xc6, 0x49, 0xf4, 0x4c, 0x0c, 0x76, 0x5b, 0x96,
    0xae, 0xab, 0x8b, 0xb3, 0x4e, 0xe8, 0x3c, 0xc7, 0xa6, 0x83, 0xc4, 0xe5,
    0x3d, 0x15, 0x81, 0xc8, 0xc7, 0x30, 0x2e, 0x06, 0x0a, 0x2b, 0x06, 0x01,
    0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x24, 0x04, 0x20, 0xdc, 0x54, 0xf2,
    0x9f, 0x4b, 0xf4, 0xc7, 0x58, 0x4a, 0x8d, 0x99, 0x15, 0xb0, 0x93, 0x59,
    0x95, 0x9f, 0x8e, 0x13, 0xf1, 0x30, 0x09, 0xc1, 0xea, 0xb4, 0xb0, 0x0c,
    0x21, 0xb8, 0x5f, 0x19, 0x30, 0x30, 0x2e, 0x06, 0x0a, 0x2b, 0x06, 0x01,
    0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x25, 0x04, 0x20, 0xdc, 0x54, 0xf2,
    0x9f, 0x4b, 0xf4, 0xc7, 0x58, 0x4a, 0x8d, 0x99, 0x15, 0xb0, 0x93, 0x59,
    0x95, 0x9f, 0x8e, 0x13, 0xf1, 0x30, 0x09, 0xc1, 0xea, 0xb4, 0xb0, 0x0c,
    0x21, 0xb8, 0x5f, 0x19, 0x30, 0x30, 0x12, 0x06, 0x0a, 0x2b, 0x06, 0x01,
    0x04, 0x01, 0xd6, 0x79, 0x02, 0x01, 0x26, 0x04, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03,
    0x02, 0x03, 0x48, 0x00, 0x30, 0x45, 0x02, 0x21, 0x00, 0xa1, 0x69, 0x90,
    0x27, 0xa8, 0x5b, 0x4b, 0x2e, 0xad, 0xc4, 0x2b, 0xc4, 0x21, 0x03, 0x45,
    0xe5, 0x34, 0xa9, 0x8e, 0x2d, 0x6b, 0x99, 0x32, 0x13, 0xa6, 0x36, 0xfb,
    0xf5, 0xd1, 0xbc, 0x89, 0x81, 0x02, 0x20, 0x60, 0x1b, 0x9e, 0x7a, 0x03,
    0x97, 0x8c, 0x16, 0xdf, 0x34, 0x98, 0x81, 0x87, 0xdf, 0x8e, 0xfa, 0xf2,
    0x8c, 0x4a, 0xb9, 0x3b, 0x6b, 0x73, 0xaf, 0xff, 0x01, 0xef, 0x31, 0x64,
    0x0d, 0x16, 0xff, 0x31, 0x00,
};

constexpr char kExpectedPem[] =
    R"(-----BEGIN CERTIFICATE-----
MIICTjCCAfSgAwIBAgIQAQAAAAAAAAADTP1bG0Lv+jAKBggqhkjOPQQDAjBoMQswCQYDVQQGEwJV
UzETMBEGA1UECBMKQ2FsaWZvcm5pYTEUMBIGA1UEChMLR29vZ2xlIEluYy4xFDASBgNVBAsTC0Vu
Z2luZWVyaW5nMRgwFgYDVQQDEw9DUk9TIEQyIFRQTSBDSUswIhgPMjAwMDAxMDEwMDAwMDBaGA8y
MDk5MTIzMTIzNTk1OVowDzENMAsGA1UEAwwEVGk1MDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IA
BANM/VsbQu/6ONV0z54EhUPF9bKWPSKn6I98p28tajLOoBmctxMFxq7I+pdF10EC2qnb7z2kXUzL
/PMMN22ed8yjgdQwgdEwGgYKKwYBBAHWeQIBIQQMWlNaVqWspal/fwAAMA8GCisGAQQB1nkCASIE
ARQwLgYKKwYBBAHWeQIBIwQgierzUTS0s8ZJ9EwMdluWrquLs07oPMemg8TlPRWByMcwLgYKKwYB
BAHWeQIBJAQg3FTyn0v0x1hKjZkVsJNZlZ+OE/EwCcHqtLAMIbhfGTAwLgYKKwYBBAHWeQIBJQQg
3FTyn0v0x1hKjZkVsJNZlZ+OE/EwCcHqtLAMIbhfGTAwEgYKKwYBBAHWeQIBJgQEAAAAADAKBggq
hkjOPQQDAgNIADBFAiEAoWmQJ6hbSy6txCvEIQNF5TSpji1rmTITpjb79dG8iYECIGAbnnoDl4wW
3zSYgYffjvryjEq5O2tzr/8B7zFkDRb/
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIB6DCCAY2gAwIBAgIKSWj2Lvak6lGiCzAKBggqhkjOPQQDAjBkMQswCQYDVQQGEwJVUzETMBEG
A1UECBMKQ2FsaWZvcm5pYTEUMBIGA1UEChMLR29vZ2xlIEluYy4xFDASBgNVBAsTC0VuZ2luZWVy
aW5nMRQwEgYDVQQDEwtDUk9TIEQyIENJSzAeFw0yMDA3MDgwMDAwMDBaFw00OTEyMzEyMzU5NTla
MGgxCzAJBgNVBAYTAlVTMRMwEQYDVQQIEwpDYWxpZm9ybmlhMRQwEgYDVQQKEwtHb29nbGUgSW5j
LjEUMBIGA1UECxMLRW5naW5lZXJpbmcxGDAWBgNVBAMTD0NST1MgRDIgVFBNIENJSzBZMBMGByqG
SM49AgEGCCqGSM49AwEHA0IABGSeOSt182ILaE/zXsMr6y6y/8tjPySLSkrJ6/Oi/kFS3yF8vgoG
+tqwq9INoMnoqC2O8rrdYwn9X0vh2FzKfc2jIzAhMB8GA1UdIwQYMBaAFJ14CUkL2Rm+LGmPU4P4
NFOwTuPyMAoGCCqGSM49BAMCA0kAMEYCIQDi9xshD9+xlcaEKNSGOz9FwRfc3BZBIa9TzWK0sde5
XwIhAIAbWbPnTW5515+ht//avxMThag+7CpD9lG7rLLLLonn
-----END CERTIFICATE-----
)";

}  // namespace

namespace hwsec {

class OpteePluginFrontendImplTpm2SimTest : public testing::Test {
 public:
  void SetUp() override {
    hwsec_optee_plugin_ = hwsec_factory_.GetOpteePluginFrontend();
    ON_CALL(ro_data, IsReady(_)).WillByDefault(ReturnValue(true));
    ON_CALL(ro_data, Read(RoSpace::kChipIdentityKeyCert))
        .WillByDefault(ReturnValue(
            brillo::Blob(std::begin(kFakeCikCert), std::end(kFakeCikCert))));
    ON_CALL(ro_data, Read(RoSpace::kWidevineRootOfTrustCert))
        .WillByDefault(ReturnValue(
            brillo::Blob(std::begin(kFakeRotCert), std::end(kFakeRotCert))));
  }

  void SetupAtMost1() {
    EXPECT_CALL(ro_data, IsReady(RoSpace::kChipIdentityKeyCert))
        .Times(AtMost(1));
    EXPECT_CALL(ro_data, IsReady(RoSpace::kWidevineRootOfTrustCert))
        .Times(AtMost(1));
    EXPECT_CALL(ro_data, Read(RoSpace::kChipIdentityKeyCert)).Times(AtMost(1));
    EXPECT_CALL(ro_data, Read(RoSpace::kWidevineRootOfTrustCert))
        .Times(AtMost(1));
  }

 protected:
  hwsec::Tpm2SimulatorFactoryForTest hwsec_factory_;
  MockRoData& ro_data = hwsec_factory_.GetMockBackend().GetMock().ro_data;
  std::unique_ptr<const OpteePluginFrontend> hwsec_optee_plugin_;
};

TEST_F(OpteePluginFrontendImplTpm2SimTest, GetPkcs7CertChain) {
  EXPECT_CALL(ro_data, IsReady(RoSpace::kChipIdentityKeyCert)).Times(1);
  EXPECT_CALL(ro_data, IsReady(RoSpace::kWidevineRootOfTrustCert)).Times(1);
  EXPECT_CALL(ro_data, Read(RoSpace::kChipIdentityKeyCert)).Times(1);
  EXPECT_CALL(ro_data, Read(RoSpace::kWidevineRootOfTrustCert)).Times(1);

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              IsOkAndHolds(brillo::Blob(std::begin(kExpectedPkcs7),
                                        std::end(kExpectedPkcs7))));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, CikNotReady) {
  SetupAtMost1();

  EXPECT_CALL(ro_data, IsReady(RoSpace::kChipIdentityKeyCert))
      .WillOnce(ReturnValue(false));

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              NotOkWith("NV space not ready"));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, RotNotReady) {
  SetupAtMost1();

  EXPECT_CALL(ro_data, IsReady(RoSpace::kWidevineRootOfTrustCert))
      .WillOnce(ReturnValue(false));

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              NotOkWith("NV space not ready"));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, CikReadError) {
  SetupAtMost1();

  EXPECT_CALL(ro_data, Read(RoSpace::kChipIdentityKeyCert))
      .WillOnce(
          ReturnError<TPMError>("CIK read error", TPMRetryAction::kNoRetry));

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              NotOkWith("CIK read error"));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, RotReadError) {
  SetupAtMost1();

  EXPECT_CALL(ro_data, Read(RoSpace::kWidevineRootOfTrustCert))
      .WillOnce(
          ReturnError<TPMError>("RoT read error", TPMRetryAction::kNoRetry));

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              NotOkWith("RoT read error"));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, EmptyCik) {
  SetupAtMost1();

  EXPECT_CALL(ro_data, Read(RoSpace::kChipIdentityKeyCert))
      .WillOnce(ReturnValue(brillo::Blob()));

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              NotOkWith("Failed to parse CIK cert"));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, MalformedRot) {
  SetupAtMost1();

  EXPECT_CALL(ro_data, Read(RoSpace::kWidevineRootOfTrustCert))
      .WillOnce(ReturnValue(brillo::BlobFromString("Wrong Rot")));

  EXPECT_THAT(hwsec_optee_plugin_->GetPkcs7CertChain(),
              NotOkWith("Failed to parse RoT cert"));
}

TEST_F(OpteePluginFrontendImplTpm2SimTest, GetPemCertChain) {
  EXPECT_CALL(ro_data, IsReady(RoSpace::kChipIdentityKeyCert)).Times(1);
  EXPECT_CALL(ro_data, IsReady(RoSpace::kWidevineRootOfTrustCert)).Times(1);
  EXPECT_CALL(ro_data, Read(RoSpace::kChipIdentityKeyCert)).Times(1);
  EXPECT_CALL(ro_data, Read(RoSpace::kWidevineRootOfTrustCert)).Times(1);

  EXPECT_THAT(hwsec_optee_plugin_->GetPemCertChain(),
              IsOkAndHolds(std::string(kExpectedPem)));
}

}  // namespace hwsec
