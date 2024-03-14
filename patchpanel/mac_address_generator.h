// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MAC_ADDRESS_GENERATOR_H_
#define PATCHPANEL_MAC_ADDRESS_GENERATOR_H_

#include <stdint.h>

#include <brillo/brillo_export.h>
#include <net-base/mac_address.h>

namespace patchpanel {

using MacAddress = std::array<uint8_t, 6>;

// Generates locally managed EUI-48 MAC addresses and ensures no collisions
// with any previously generated addresses by this instance.
class BRILLO_EXPORT MacAddressGenerator {
 public:
  // Base address used for MacAddressGenerator::GetStable.
  static constexpr net_base::MacAddress::DataType kStableBaseAddr = {
      0x42, 0x37, 0x05, 0x13, 0x17, 0x00};

  MacAddressGenerator() = default;
  MacAddressGenerator(const MacAddressGenerator&) = delete;
  MacAddressGenerator& operator=(const MacAddressGenerator&) = delete;

  ~MacAddressGenerator() = default;

  // Generates a new EUI-48 MAC address and ensures that there are no
  // collisions with any addresses previously generated by this instance of
  // the generator.
  net_base::MacAddress Generate();

  // Returns a stable MAC address whose first 5 octets are fixed and using the
  // least significant byte of |id|
  // as the sixth. The base address is itself random and was not generated from
  // any particular device, physical or virtual. Additionally, the |id| should
  // associated with any specific device either, and should be set
  // independently.
  net_base::MacAddress GetStable(uint32_t id) const;

 private:
  // Set of all addresses generated by this instance.  This doesn't _need_ to be
  // an unordered_set but making it one improves the performance of the
  // "Duplicates" unit test by ~33% (~150 seconds -> ~100 seconds) and it
  // doesn't have a huge impact in production use so that's why we use it here.
  net_base::MacAddress::UnorderedSet addrs_;
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MAC_ADDRESS_GENERATOR_H_
