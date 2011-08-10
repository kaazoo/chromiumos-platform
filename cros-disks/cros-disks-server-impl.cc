// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/cros-disks-server-impl.h"

#include <base/logging.h>

#include "cros-disks/archive-manager.h"
#include "cros-disks/device-event.h"
#include "cros-disks/disk.h"
#include "cros-disks/disk-manager.h"
#include "cros-disks/format-manager.h"
#include "cros-disks/platform.h"

using std::string;
using std::vector;

namespace cros_disks {

// TODO(rtc): this should probably be a flag.
// TODO(benchan): move these to common/chromeos/dbus/service_constants.
static const char kServicePath[] = "/org/chromium/CrosDisks";
static const char kServiceErrorName[] = "org.chromium.CrosDisks.Error";
static const char kPropertyExperimentalFeaturesEnabled[] =
  "ExperimentalFeaturesEnabled";

CrosDisksServer::CrosDisksServer(DBus::Connection& connection,  // NOLINT
                                 Platform* platform,
                                 ArchiveManager* archive_manager,
                                 DiskManager* disk_manager,
                                 FormatManager* format_manager)
    : DBus::ObjectAdaptor(connection, kServicePath),
      platform_(platform),
      archive_manager_(archive_manager),
      disk_manager_(disk_manager),
      format_manager_(format_manager) {
  CHECK(platform_) << "Invalid platform object";
  CHECK(archive_manager_) << "Invalid archive manager object";
  CHECK(disk_manager_) << "Invalid disk manager object";
  CHECK(format_manager_) << "Invalid format manager object";

  // TODO(benchan): Refactor the code so that we don't have to pass
  //                DiskManager, ArchiveManager, etc to the constructor
  //                of CrosDisksServer, but instead pass a list of mount
  //                managers.
  mount_managers_.push_back(disk_manager_);
  mount_managers_.push_back(archive_manager_);

  InitializeProperties();
  format_manager_->set_parent(this);
}

CrosDisksServer::~CrosDisksServer() {
}

bool CrosDisksServer::IsAlive(DBus::Error& error) {  // NOLINT
  return true;
}

string CrosDisksServer::GetDeviceFilesystem(const string& device_path,
                                            DBus::Error& error) {  // NOLINT
  return disk_manager_->GetFilesystemTypeOfDevice(device_path);
}

void CrosDisksServer::SignalFormattingFinished(const string& device_path,
                                               int status) {
  if (status) {
    FormattingFinished(std::string("!") + device_path);
    LOG(ERROR) << "Could not format device '" << device_path
               << "'. Formatting process failed with an exit code " << status;
  } else {
    FormattingFinished(device_path);
  }
}

bool CrosDisksServer::FormatDevice(const string& device_path,
                                   const string& filesystem,
                                   DBus::Error &error) {  // NOLINT
  if (!format_manager_->StartFormatting(device_path, filesystem)) {
    LOG(ERROR) << "Could not format device " << device_path
               << " as file system '" << filesystem << "'";
    return false;
  }
  return true;
}

// TODO(benchan): Deprecate this method.
string CrosDisksServer::FilesystemMount(const string& device_path,
                                        const string& filesystem_type,
                                        const vector<string>& mount_options,
                                        DBus::Error& error) {  // NOLINT
  string mount_path;
  if (disk_manager_->Mount(device_path, filesystem_type, mount_options,
                           &mount_path) == kMountErrorNone) {
    DiskChanged(device_path);
  } else {
    string message = "Could not mount device " + device_path;
    LOG(ERROR) << message;
    error.set(kServiceErrorName, message.c_str());
  }
  return mount_path;
}

// TODO(benchan): Deprecate this method.
void CrosDisksServer::FilesystemUnmount(const string& device_path,
                                        const vector<string>& mount_options,
                                        DBus::Error& error) {  // NOLINT
  if (disk_manager_->Unmount(device_path, mount_options) != kMountErrorNone) {
    string message = "Could not unmount device " + device_path;
    LOG(ERROR) << message;
    error.set(kServiceErrorName, message.c_str());
  }
}

void CrosDisksServer::Mount(const string& path,
                            const string& filesystem_type,
                            const vector<string>& options,
                            DBus::Error& error) {  // NOLINT
  MountErrorType error_type = kMountErrorInvalidPath;
  MountSourceType source_type = kMountSourceInvalid;
  string mount_path;

  for (vector<MountManager*>::iterator manager_iter = mount_managers_.begin();
       manager_iter != mount_managers_.end(); ++manager_iter) {
    MountManager* manager = *manager_iter;
    if (manager->CanMount(path)) {
      source_type = manager->GetMountSourceType();
      error_type = manager->Mount(path, filesystem_type, options, &mount_path);
      break;
    }
  }

  if (error_type == kMountErrorNone) {
    // TODO(benchan): Remove this DiskChanged signal when UI
    // no longer requires it.
    DiskChanged(path);
  } else {
    LOG(ERROR) << "Failed to mount '" << path << "'";
  }
  MountCompleted(error_type, path, source_type, mount_path);
}

void CrosDisksServer::Unmount(const string& path,
                              const vector<string>& options,
                              DBus::Error& error) {  // NOLINT
  MountErrorType error_type = kMountErrorInvalidPath;
  for (vector<MountManager*>::iterator manager_iter = mount_managers_.begin();
       manager_iter != mount_managers_.end(); ++manager_iter) {
    MountManager* manager = *manager_iter;
    if (manager->CanUnmount(path)) {
      error_type = manager->Unmount(path, options);
      break;
    }
  }

  if (error_type != kMountErrorNone) {
    string message = "Failed to unmount '" + path + "'";
    error.set(kServiceErrorName, message.c_str());
  }
}

vector<string> CrosDisksServer::DoEnumerateDevices(
    bool auto_mountable_only) const {
  vector<Disk> disks = disk_manager_->EnumerateDisks();
  vector<string> devices;
  devices.reserve(disks.size());
  for (vector<Disk>::const_iterator disk_iterator = disks.begin();
       disk_iterator != disks.end(); ++disk_iterator) {
    if (!auto_mountable_only || disk_iterator->is_auto_mountable()) {
      devices.push_back(disk_iterator->native_path());
    }
  }
  return devices;
}

vector<string> CrosDisksServer::EnumerateDevices(
    DBus::Error& error) {  // NOLINT
  return DoEnumerateDevices(false);
}

vector<string> CrosDisksServer::EnumerateAutoMountableDevices(
    DBus::Error& error) {  // NOLINT
  return DoEnumerateDevices(true);
}

DBusDisk CrosDisksServer::GetDeviceProperties(const string& device_path,
                                              DBus::Error& error) {  // NOLINT
  Disk disk;
  if (!disk_manager_->GetDiskByDevicePath(device_path, &disk)) {
    string message = "Could not get the properties of device " + device_path;
    LOG(ERROR) << message;
    error.set(kServiceErrorName, message.c_str());
  }
  return disk.ToDBusFormat();
}

void CrosDisksServer::OnSessionStarted(const string& user) {
  for (vector<MountManager*>::iterator manager_iter = mount_managers_.begin();
       manager_iter != mount_managers_.end(); ++manager_iter) {
    MountManager* manager = *manager_iter;
    manager->StartSession(user);
  }
}

void CrosDisksServer::OnSessionStopped(const string& user) {
  for (vector<MountManager*>::iterator manager_iter = mount_managers_.begin();
       manager_iter != mount_managers_.end(); ++manager_iter) {
    MountManager* manager = *manager_iter;
    manager->StopSession(user);
  }
}

void CrosDisksServer::DispatchDeviceEvent(const DeviceEvent& event) {
  switch (event.event_type) {
    case DeviceEvent::kDeviceAdded:
      DeviceAdded(event.device_path);
      break;
    case DeviceEvent::kDeviceScanned:
      DeviceScanned(event.device_path);
      break;
    case DeviceEvent::kDeviceRemoved:
      DeviceRemoved(event.device_path);
      break;
    case DeviceEvent::kDiskAdded:
      DiskAdded(event.device_path);
      break;
    case DeviceEvent::kDiskAddedAfterRemoved:
      DiskRemoved(event.device_path);
      DiskAdded(event.device_path);
      break;
    case DeviceEvent::kDiskChanged:
      DiskChanged(event.device_path);
      break;
    case DeviceEvent::kDiskRemoved:
      DiskRemoved(event.device_path);
      break;
    default:
      break;
  }
}

void CrosDisksServer::InitializeProperties() {
  try {
    DBus::Variant value;
    value.writer().append_bool(platform_->experimental_features_enabled());
    CrosDisks_adaptor::set_property(kPropertyExperimentalFeaturesEnabled,
                                    value);
  } catch (const DBus::Error& e) {  // NOLINT
    LOG(FATAL) << "Failed to initialize properties: " << e.what();
  }
}

void CrosDisksServer::on_set_property(
    DBus::InterfaceAdaptor& interface,  // NOLINT
    const string& property, const DBus::Variant& value) {
  if (property == kPropertyExperimentalFeaturesEnabled) {
    platform_->set_experimental_features_enabled(value.reader().get_bool());
  }
}

}  // namespace cros_disks
