// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_id/client_id.h"

#include <iostream>
#include <map>
#include <utility>

#include <base/containers/contains.h>
#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace client_id {

namespace {

constexpr char kClientIdPrefix[] = "Reven-";
constexpr char kClientIdFile[] = "var/lib/client_id/client_id";
constexpr char kUuidPath[] = "proc/sys/kernel/random/uuid";
constexpr char kLegacyClientIdFile[] =
    "mnt/stateful_partition/cloudready/client_id";
constexpr char kDmiSerialPath[] = "sys/devices/virtual/dmi/id/product_serial";
constexpr char kNetworkInterfacesPath[] = "sys/class/net";
constexpr int kMinSerialLength = 2;
const char* kBadSerials[] = {"to be filled by o.e.m.",
                             "to be filled by o.e.m",
                             "123456789",
                             "system serial number",
                             "invalid",
                             "none",
                             "default string",
                             "not applicable",
                             "na",
                             "ssn12345678901234567",
                             "system serial#",
                             "1234567",
                             "systemserialnumb",
                             "serial#",
                             "oem",
                             "default_string",
                             "$serialnumber$"};
constexpr char kInterfaceAddressFile[] = "address";
constexpr char kInterfaceModAliasFile[] = "device/modalias";
constexpr char kInterfaceUsbPrefix[] = "usb:";
const char* kPriorityInterfaces[] = {"eth0", "wlan0"};
const char* kBadInterfacePrefixes[] = {"arc", "docker"};
const char* kBadMacs[] = {"00:00:00:00:00:00"};

base::Optional<std::string> ReadAndTrimFile(const base::FilePath& file_path) {
  std::string out;
  if (!base::ReadFileToString(file_path, &out))
    return base::nullopt;

  base::TrimWhitespaceASCII(out, base::TRIM_ALL, &out);

  return out;
}

bool InterfaceIsInteresting(const std::string& name,
                            const std::string& address) {
  // an interesting interface is one that is not in the list of bad
  // interface name prefixes or in the list of bad mac addresses.

  // compare the interface name with the list of bad names by prefix.
  for (std::size_t i = 0; i < std::size(kBadInterfacePrefixes); i++) {
    if (base::StartsWith(name, kBadInterfacePrefixes[i],
                         base::CompareCase::INSENSITIVE_ASCII))
      return false;
  }

  // compare the interface address with the list of bad addresses.
  if (base::Contains(kBadMacs, address))
    return false;

  return true;
}

bool InterfaceIsUsb(const base::FilePath& modalias_path) {
  // usb interfaces should not be relied on as they can be removable devices.
  // the bus is determined by reading the modalias for a given interface name.
  const auto modalias = ReadAndTrimFile(modalias_path);
  // if we can't read the interface, ignore it.
  if (!modalias)
    return true;

  // check for usb prefix in the modalias.
  if (base::StartsWith(modalias.value(), kInterfaceUsbPrefix,
                       base::CompareCase::INSENSITIVE_ASCII))
    return true;

  return false;
}

}  // namespace

ClientIdGenerator::ClientIdGenerator(const base::FilePath& base_path) {
  base_path_ = base_path;
}

base::Optional<std::string> ClientIdGenerator::AddClientIdPrefix(
    const std::string& client_id) {
  return kClientIdPrefix + client_id;
}

base::Optional<std::string> ClientIdGenerator::ReadClientId() {
  const base::FilePath client_id_path = base_path_.Append(kClientIdFile);

  return ReadAndTrimFile(client_id_path);
}

base::Optional<std::string> ClientIdGenerator::TryLegacy() {
  base::Optional<std::string> legacy;
  const base::FilePath legacy_path = base_path_.Append(kLegacyClientIdFile);

  if (!(legacy = ReadAndTrimFile(legacy_path)))
    return base::nullopt;
  if (legacy.value().empty())
    return base::nullopt;

  return legacy;
}

base::Optional<std::string> ClientIdGenerator::TrySerial() {
  base::Optional<std::string> serial;
  const base::FilePath serial_path = base_path_.Append(kDmiSerialPath);

  // check if serial is present.
  if (!(serial = ReadAndTrimFile(serial_path)))
    return base::nullopt;

  // check if the serial is long enough.
  if (serial.value().length() < kMinSerialLength)
    return base::nullopt;

  // check if the serial is not made up of a single repeated character.
  std::size_t found = serial.value().find_first_not_of(serial.value()[0]);
  if (found == std::string::npos)
    return base::nullopt;

  // check if the serial is in the bad serials list.
  if (base::Contains(kBadSerials, serial))
    return base::nullopt;

  return serial;
}

base::Optional<std::string> ClientIdGenerator::TryMac() {
  std::map<std::string, std::string> interfaces;

  const base::FilePath interfaces_path =
      base_path_.Append(kNetworkInterfacesPath);

  // loop through sysfs network interfaces
  base::FileEnumerator interface_dirs(interfaces_path, false,
                                      base::FileEnumerator::DIRECTORIES);
  for (base::FilePath interface_dir = interface_dirs.Next();
       !interface_dir.empty(); interface_dir = interface_dirs.Next()) {
    std::string name = interface_dir.BaseName().value();
    base::FilePath address_file_path =
        interfaces_path.Append(name).Append(kInterfaceAddressFile);
    base::Optional<std::string> address;

    // skip the interface if it has no address
    if (!(address = ReadAndTrimFile(address_file_path)))
      continue;

    // check if the interface qualifies as interesting
    if (InterfaceIsInteresting(name, address.value())) {
      interfaces.insert(
          std::pair<std::string, std::string>(name, address.value()));
    }
  }

  // try priority interfaces (usb is allowed for priority interfaces).
  for (std::size_t i = 0; i < std::size(kPriorityInterfaces); i++) {
    if (interfaces.count(kPriorityInterfaces[i])) {
      return interfaces[kPriorityInterfaces[i]];
    }
  }

  // try remaining interfaces
  for (const auto& interface : interfaces) {
    // skip usb interfaces
    base::FilePath modalias_path = base_path_.Append(kNetworkInterfacesPath)
                                       .Append(interface.first)
                                       .Append(kInterfaceModAliasFile);
    if (InterfaceIsUsb(modalias_path))
      continue;

    return interface.second;
  }

  return base::nullopt;
}

base::Optional<std::string> ClientIdGenerator::TryUuid() {
  const base::FilePath uuid_path = base_path_.Append(kUuidPath);

  return ReadAndTrimFile(uuid_path);
}

bool ClientIdGenerator::WriteClientId(const std::string& client_id) {
  const base::FilePath client_id_file_path = base_path_.Append(kClientIdFile);
  if (base::CreateDirectory(client_id_file_path.DirName())) {
    return base::WriteFile(client_id_file_path, client_id + "\n");
  }
  return false;
}

base::Optional<std::string> ClientIdGenerator::GenerateAndSaveClientId() {
  base::Optional<std::string> client_id;

  // Check for existing client_id and exit early.
  if ((client_id = ReadClientId())) {
    LOG(INFO) << "Found existing client_id: " << client_id.value();
    return client_id;
  }

  if ((client_id = TryLegacy())) {
    LOG(INFO) << "Using CloudReady legacy client_id: " << client_id.value();
  } else if ((client_id = TrySerial())) {
    client_id = AddClientIdPrefix(client_id.value());
    LOG(INFO) << "Using DMI serial number for client_id: " << client_id.value();
  } else if ((client_id = TryMac())) {
    client_id = AddClientIdPrefix(client_id.value());
    LOG(INFO) << "Using MAC address for client_id: " << client_id.value();
  } else if ((client_id = TryUuid())) {
    client_id = AddClientIdPrefix(client_id.value());
    LOG(INFO) << "Using random UUID for client_id: " << client_id.value();
  } else {
    LOG(ERROR) << "No valid client_id source was found";
    return base::nullopt;
  }

  // save result
  if (WriteClientId(client_id.value())) {
    LOG(INFO) << "Successfully wrote client_id: " << client_id.value();
    return client_id;
  }

  return base::nullopt;
}

}  // namespace client_id
