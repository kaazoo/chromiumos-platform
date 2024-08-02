// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_FEATURES_H_
#define ODML_ON_DEVICE_MODEL_FEATURES_H_

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

extern "C" {

// The valid feature types.
enum class Feature : uint32_t;

// Signature of the FormatInput() function which the shared library exports.
using FormatInputSignature = std::optional<std::string> (*)(
    const std::string& uuid,
    Feature feature,
    const std::unordered_map<std::string, std::string>& fields);

inline constexpr const char kFormatInputName[] = "FormatInput";

}  // extern "C"

#endif  // ODML_ON_DEVICE_MODEL_FEATURES_H_
