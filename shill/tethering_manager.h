// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TETHERING_MANAGER_H_
#define SHILL_TETHERING_MANAGER_H_

#include <memory>
#include <string>

#include "shill/refptr_types.h"
#include "shill/store/property_store.h"
#include "shill/technology.h"
#include "shill/wifi/local_device.h"
#include "shill/wifi/local_service.h"
#include "shill/wifi/wifi_rf.h"
#include "shill/wifi/wifi_security.h"

namespace shill {

class Manager;
class StoreInterface;

// TetheringManager handles tethering related logics. It is created by the
// Manager class.
//
// It reuses the Profile class to persist the tethering parameters for each
// user. Without user's input, it uses the default tethering configuration with
// a random SSID and a random passphrase. It saves the current tethering
// configuration to user profile when the user sets tethering config, or user
// enables tethering.
//
// It interacts with HotspotDevice,
// CellularServiceProvider and EthernetProvider classes to prepare upstream and
// downstream technologies. It interacts with patchpanel via dbus to set up the
// tethering network.
class TetheringManager {
 public:
  enum class EntitlementStatus {
    kReady,
    kNotAllowed,
    kUpstreamNetworkNotAvailable,
  };

  static const char* EntitlementStatusName(EntitlementStatus status);

  enum class SetEnabledResult {
    kSuccess,
    kFailure,
    kNotAllowed,
    kInvalidProperties,
    kUpstreamNetworkNotAvailable,
  };

  static const std::string SetEnabledResultName(SetEnabledResult result);

  enum class TetheringState {
    kTetheringIdle,
    kTetheringStarting,
    kTetheringActive,
  };

  // Storage group for tethering configs.
  static constexpr char kStorageId[] = "tethering";

  explicit TetheringManager(Manager* manager);
  TetheringManager(const TetheringManager&) = delete;
  TetheringManager& operator=(const TetheringManager&) = delete;

  virtual ~TetheringManager();

  // Initialize DBus properties related to tethering.
  void InitPropertyStore(PropertyStore* store);
  // Start and initialize TetheringManager.
  void Start();
  // Stop TetheringManager.
  void Stop();
  // Enable or disable a tethering session with existing tethering config.
  void SetEnabled(bool enabled,
                  base::OnceCallback<void(SetEnabledResult result)> callback);
  // Check if upstream network is ready for tethering.
  void CheckReadiness(
      base::OnceCallback<void(EntitlementStatus result)> callback);
  // Load the tethering config available in |profile| if there was any tethering
  // config saved for this |profile|.
  virtual void LoadConfigFromProfile(const ProfileRefPtr& profile);
  // Unload the tethering config related to |profile| and reset the tethering
  // config with default values.
  virtual void UnloadConfigFromProfile();
  static const char* TetheringStateName(const TetheringState& state);
  // Get the current TetheringStatus dictionary.
  KeyValueStore GetStatus();

 private:
  friend class TetheringManagerTest;
  FRIEND_TEST(TetheringManagerTest, FromProperties);
  FRIEND_TEST(TetheringManagerTest, GetCapabilities);
  FRIEND_TEST(TetheringManagerTest, GetConfig);
  FRIEND_TEST(TetheringManagerTest, GetTetheringCapabilities);
  FRIEND_TEST(TetheringManagerTest, SaveConfig);
  FRIEND_TEST(TetheringManagerTest, SetEnabled);

  using SetEnabledResultCallback =
      base::OnceCallback<void(SetEnabledResult result)>;

  // Tethering properties get handlers.
  KeyValueStore GetCapabilities(Error* error);
  KeyValueStore GetConfig(Error* error);
  KeyValueStore GetStatus(Error* error) { return GetStatus(); }

  bool SetAndPersistConfig(const KeyValueStore& config, Error* error);
  // Populate the shill D-Bus parameter map |properties| with the
  // parameters contained in |this| and return true if successful.
  bool ToProperties(KeyValueStore* properties) const;
  // Populate tethering config from a dictionary.
  bool FromProperties(const KeyValueStore& properties);
  // Reset tethering config with default value and a random WiFi SSID and
  // a random passphrase.
  void ResetConfiguration();
  // Save the current tethering config to user's profile.
  bool Save(StoreInterface* storage);
  // Load the current tethering config from user's profile.
  bool Load(const StoreInterface* storage);
  // Set tethering state and emit dbus property changed signal.
  void SetState(TetheringState state);
  void OnDownstreamDeviceEvent(LocalDevice::DeviceEvent event,
                               const LocalDevice* device);
  // Trigger callback function asynchronously to post SetTetheringEnabled dbus
  // result.
  void PostSetEnabledResult(SetEnabledResult result);
  // Check if all the tethering resources are ready. If so post the
  // SetTetheringEnabled dbus result.
  void CheckAndPostTetheringResult();
  // Prepare tethering resources to start a tethering session.
  void StartTetheringSession();
  // Stop and free tethering resources.
  void StopTetheringSession();

  // TetheringManager is created and owned by Manager.
  Manager* manager_;
  // Tethering feature flag.
  bool allowed_;
  // Tethering state as listed in enum TetheringState.
  TetheringState state_;

  // Automatically disable tethering if no devices have been associated for
  // |kAutoDisableMinute| minutes.
  bool auto_disable_;
  // MAC address randomization. When it is true, AP will use a randomized MAC
  // each time it is started. If false, it will use the persisted MAC address.
  bool mar_;
  // The hex-encoded tethering SSID name to be used in WiFi downstream.
  std::string hex_ssid_;
  // The passphrase to be used in WiFi downstream.
  std::string passphrase_;
  // The security mode to be used in WiFi downstream.
  WiFiSecurity security_;
  // The preferred band to be used in WiFi downstream.
  WiFiBand band_;
  // Preferred upstream technology to use.
  Technology upstream_technology_;

  // Member to hold the result callback function. This callback function gets
  // set when dbus method SetTetheringEnabled is called and runs when the async
  // method call is done.
  SetEnabledResultCallback result_callback_;
  // Downlink hotspot device.
  HotspotDeviceRefPtr hotspot_dev_;
  // If downstream hotspot device event kServiceUp has been received or not.
  bool hotspot_service_up_;
};

inline std::ostream& operator<<(std::ostream& stream,
                                TetheringManager::TetheringState state) {
  return stream << TetheringManager::TetheringStateName(state);
}

}  // namespace shill

#endif  // SHILL_TETHERING_MANAGER_H_
