// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sommelier.h"          // NOLINT(build/include_directory)
#include "sommelier-tracing.h"  // NOLINT(build/include_directory)

#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include "libevdev/libevdev-shim.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>

#include "gaming-input-unstable-v2-client-protocol.h"  // NOLINT(build/include_directory)

// Overview of state management via gaming events, in order:
// 1) Acquire gaming seats (in sommelier.cc)
// 2) Add listeners to gaming seats
// 3) Listen for zcr_gaming_seat_v2.gamepad_added to construct a 'default'
//    game controller (not currently implemented)
//    Calls libevdev_new, libevdev_enable_event_type,
//          libevdev_uinput_create_from_device
// 4) Listen for zcr_gaming_seat_v2.gamepad_added_with_device_info to construct
//    a custom game controller
//    Calls libevdev_new
// 5) Listen for zcr_gamepad_v2.axis_added to fill in a custom game controller
//    Calls libevdev_enable_event_type
// 6) Listen for zcr_gamepad_v2.activated to finalize a custom game controller
//    Calls libevdev_uinput_create_from_device
// 7) Listen for zcr_gamepad_v2.axis to set frame state for game controller
//    Calls libevdev_uinput_write_event
// 8) Listen for zcr_gamepad_v2.button to set frame state for game controller
//    Calls libevdev_uinput_write_event
// 9) Listen for zcr_gamepad_v2.frame to emit collected frame
//    Calls libevdev_uinput_write_event(EV_SYN)
// 10) Listen for zcr_gamepad_v2.removed to destroy gamepad
//    Must handle gamepads in all states of construction or error

enum GamepadActivationState {
  kStateUnknown = 0,    // Should not happen
  kStatePending = 1,    // Constructed, pending axis definition
  kStateActivated = 2,  // Fully activated
  kStateError = 3       // Error occurred during construction; ignore gracefully
};

struct DeviceID {
  const uint32_t vendor;
  const uint32_t product;
  const uint32_t version;

  bool operator==(const DeviceID& other) const {
    return vendor == other.vendor && product == other.product &&
           version == other.version;
  }
};

// Simple Hash/equal operators for DeviceID struct. Doesn't need to be efficient
// as the size of our map will be small.
template <>
struct std::hash<DeviceID> {
  std::size_t operator()(const DeviceID& device) const {
    return device.vendor ^ device.product ^ device.version;
  }
};

// Buttons being emulated by libevdev uinput.
// Note: Do not enable BTN_TL2 or BTN_TR2, as they will significantly
// change the Linux joydev interpretation of the triggers on ABS_Z/ABS_RZ.
const int kButtons[] = {BTN_SOUTH,  BTN_EAST,  BTN_NORTH,  BTN_WEST,
                        BTN_TL,     BTN_TR,    BTN_THUMBL, BTN_THUMBR,
                        BTN_SELECT, BTN_START, BTN_MODE};

// ID constants for identifying gamepads. Note that some gamepads
// may share the same vendor if they're from the same brand. Bluetooth (BT)
// and USB variants may also share the same product id - though this is
// not guaranteed.

// IDs for emulated controllers.
const char kXboxName[] = "Microsoft X-Box One S pad";
const uint32_t kUsbBus = 0x03;
const uint32_t kXboxVendor = 0x45e;
const uint32_t kXboxProduct = 0x2ea;
const uint32_t kXboxVersion = 0x301;

// Note: the Bluetooth (BT) vendor ID for SteelSeries is due to a chipset bug
// and is not an actual claimed Vendor ID.
const uint32_t kSteelSeriesBTVendor = 0x111;

const uint32_t kStadiaVendor = 0x18d1;
const uint32_t kStadiaProduct = 0x9400;

const uint32_t kSonyVendor = 0x54C;
const uint32_t kDualSenseProduct = 0xCE6;
const uint32_t kDualShock4Product = 0x9CC;

const DeviceID kStadiaUSB = {
    .vendor = kStadiaVendor, .product = kStadiaProduct, .version = 0x111};

const DeviceID kStadiaBT = {
    .vendor = kStadiaVendor, .product = kStadiaProduct, .version = 0x100};

const DeviceID kStratusDuoBT = {
    .vendor = kSteelSeriesBTVendor, .product = 0x1431, .version = 0x11B};

const DeviceID kStratusPlusBT = {
    .vendor = kSteelSeriesBTVendor, .product = 0x1434, .version = 0x216};

// DualSense versions are the HID specification versions (bcdHID). We care about
// these versions as hid-playstation and hid-sony use bcdHID to signal that the
// broken hid-generic mapping is not used.
const DeviceID kDualSenseUSB = {
    .vendor = kSonyVendor, .product = kDualSenseProduct, .version = 0x111};

const DeviceID kDualSenseBT = {
    .vendor = kSonyVendor, .product = kDualSenseProduct, .version = 0x100};

const DeviceID kDualShock4USB = {
    .vendor = kSonyVendor, .product = kDualShock4Product, .version = 0x8111};

const DeviceID kDualShock4BT = {
    .vendor = kSonyVendor, .product = kDualShock4Product, .version = 0x8100};

const DeviceID kXboxSeriesXBT = {
    .vendor = kXboxVendor, .product = 0xB13, .version = 0x513};

const DeviceID kXboxOneSOldBT = {
    .vendor = kXboxVendor, .product = 0x2E0, .version = 0x903};

const DeviceID kXboxOneS2016BT = {
    .vendor = kXboxVendor, .product = 0x2FD, .version = 0x903};

const DeviceID kXboxOneSUpdatedBT = {
    .vendor = kXboxVendor, .product = 0xB20, .version = 0x517};

const DeviceID kXboxAdaptiveBT = {
    .vendor = kXboxVendor, .product = 0xB21, .version = 0x511};

const DeviceID kXboxElite2BT = {
    .vendor = kXboxVendor, .product = 0xB22, .version = 0x511};

// Mappings from the input event of a given gamepad (key) to the
// appropriate output event (value). These mappings are intended to maintain
// the locality of a gamepad; i.e the left face button should map to a
// left face button event. Input events not represented in a map will be
// discarded.

// DualSense (PS5).
const std::unordered_map<uint32_t, uint32_t> kDualSenseMapping = {
    // Left Joystick
    {ABS_X, ABS_X},
    {ABS_Y, ABS_Y},
    // Right Joystick
    {ABS_Z, ABS_RX},
    {ABS_RZ, ABS_RY},
    // Joystick press
    {BTN_SELECT, BTN_THUMBL},
    {BTN_START, BTN_THUMBR},
    // DPad
    {ABS_HAT0X, ABS_HAT0X},
    {ABS_HAT0Y, ABS_HAT0Y},
    // Face Buttons
    {BTN_B, BTN_A},
    {BTN_C, BTN_B},
    {BTN_A, BTN_X},
    {BTN_X, BTN_Y},
    // Left bumper and trigger
    {BTN_Y, BTN_TL},
    {ABS_RX, ABS_Z},
    // Right bumper and trigger
    {BTN_Z, BTN_TR},
    {ABS_RY, ABS_RZ},
    // Menu buttons
    {BTN_TL2, BTN_SELECT},
    {BTN_TR2, BTN_START},
    {BTN_MODE, BTN_MODE},
    // Unused buttons: Touchpad_click: BTN_THUMBL, Microphone_button: BTN_THUMBR
};

// DualShock4 (PS4).
const std::unordered_map<uint32_t, uint32_t> kDualShock4Mapping = {
    // Left Joystick
    {ABS_X, ABS_X},
    {ABS_Y, ABS_Y},
    // Right Joystick
    {ABS_RX, ABS_RX},
    {ABS_RY, ABS_RY},
    // Joystick press
    {BTN_THUMBL, BTN_THUMBL},
    {BTN_THUMBR, BTN_THUMBR},
    // DPad
    {ABS_HAT0X, ABS_HAT0X},
    {ABS_HAT0Y, ABS_HAT0Y},
    // Right-hand Buttons
    {BTN_A, BTN_A},
    {BTN_B, BTN_B},
    {BTN_X, BTN_Y},
    {BTN_Y, BTN_X},
    // Left bumper and trigger
    {BTN_TL, BTN_TL},
    {ABS_Z, ABS_Z},
    // Right bumper and trigger
    {BTN_TR, BTN_TR},
    {ABS_RZ, ABS_RZ},
    // Menu buttons
    {BTN_SELECT, BTN_SELECT},
    {BTN_START, BTN_START},
    {BTN_MODE, BTN_MODE},
};

// Represents how the input events of a certain controllers (key) should be
// interpreted (value). So far this pattern has been observed in:
// - Stadia
// - Stratus Duo (BT)
// - Stratus + (BT)
// - Xbox Series X (BT)
// - Xbox One S (updated firmware) (BT)
// - Xbox Adaptive (BT)
// - Xbox Elite 2 (BT)
const std::unordered_map<uint32_t, uint32_t> kAxisQuirkMapping = {
    // Left Joystick
    {ABS_X, ABS_X},
    {ABS_Y, ABS_Y},
    // Right Joystick
    {ABS_Z, ABS_RX},
    {ABS_RZ, ABS_RY},
    // Joystick press
    {BTN_THUMBL, BTN_THUMBL},
    {BTN_THUMBR, BTN_THUMBR},
    // DPad
    {ABS_HAT0X, ABS_HAT0X},
    {ABS_HAT0Y, ABS_HAT0Y},
    // Face Buttons
    {BTN_A, BTN_A},
    {BTN_B, BTN_B},
    {BTN_X, BTN_X},
    {BTN_Y, BTN_Y},
    // Left bumper and trigger
    {BTN_TL, BTN_TL},
    {ABS_BRAKE, ABS_Z},
    // Right bumper and trigger
    {BTN_TR, BTN_TR},
    {ABS_GAS, ABS_RZ},
    // Menu buttons
    {BTN_SELECT, BTN_SELECT},
    {BTN_START, BTN_START},
    {BTN_MODE, BTN_MODE},
};

// Xbox One S (BT) - Old firmware.
// Note: this mapping is based off of a mapping from another feature
// and has not been explicitly tested. See b/277829347.
const std::unordered_map<uint32_t, uint32_t> kXboxOneSOldMapping = {
    // Left Joystick
    {ABS_X, ABS_X},
    {ABS_Y, ABS_Y},
    // Right Joystick
    {ABS_RX, ABS_RX},
    {ABS_RY, ABS_RY},
    // Joystick press
    {BTN_TL2, BTN_THUMBL},
    {BTN_TR2, BTN_THUMBR},
    // DPad
    {ABS_HAT0X, ABS_HAT0X},
    {ABS_HAT0Y, ABS_HAT0Y},
    // Face Buttons
    {BTN_A, BTN_A},
    {BTN_B, BTN_B},
    {BTN_C, BTN_X},
    {BTN_X, BTN_Y},
    // Left bumper and trigger
    {BTN_Y, BTN_TL},
    {ABS_Z, ABS_Z},
    // Right bumper and trigger
    {BTN_Z, BTN_TR},
    {ABS_RZ, ABS_RZ},
    // Menu buttons
    {BTN_TL, BTN_SELECT},
    {BTN_TR, BTN_START},
    {0x8b, BTN_MODE},
};

// Xbox One S (BT) - 2016 firmware.
const std::unordered_map<uint32_t, uint32_t> kXboxOneS2016Mapping = {
    // Left Joystick
    {ABS_X, ABS_X},
    {ABS_Y, ABS_Y},
    // Right Joystick
    {ABS_Z, ABS_RX},
    {ABS_RZ, ABS_RY},
    // Joystick press
    {BTN_THUMBL, BTN_THUMBL},
    {BTN_THUMBR, BTN_THUMBR},
    // DPad
    {ABS_HAT0X, ABS_HAT0X},
    {ABS_HAT0Y, ABS_HAT0Y},
    // Face Buttons
    {BTN_A, BTN_A},
    {BTN_B, BTN_B},
    {BTN_X, BTN_X},
    {BTN_Y, BTN_Y},
    // Left bumper and trigger
    {BTN_TL, BTN_TL},
    {ABS_BRAKE, ABS_Z},
    // Right bumper and trigger
    {BTN_TR, BTN_TR},
    {ABS_GAS, ABS_RZ},
    // Menu buttons
    {KEY_BACK, BTN_SELECT},
    {BTN_START, BTN_START},
    {KEY_HOMEPAGE, BTN_MODE},
};

// Map of devices to their respctive input remappings.
const std::unordered_map<DeviceID,
                         const std::unordered_map<uint32_t, uint32_t>*>
    kDeviceMappings = {
        {kStadiaUSB, &kAxisQuirkMapping},
        {kStadiaBT, &kAxisQuirkMapping},
        // Note that the BTN_MODE is not mapped correctly for the
        // Stratus Duo, due to it being interpreted on the host
        // as a key event causing a browser HOME action.
        {kStratusDuoBT, &kAxisQuirkMapping},
        {kStratusPlusBT, &kAxisQuirkMapping},
        {kDualSenseUSB, &kDualSenseMapping},
        {kDualSenseBT, &kDualSenseMapping},
        {kDualShock4USB, &kDualShock4Mapping},
        {kDualShock4BT, &kDualShock4Mapping},
        {kXboxSeriesXBT, &kAxisQuirkMapping},
        {kXboxOneSOldBT, &kXboxOneSOldMapping},
        {kXboxOneS2016BT, &kXboxOneS2016Mapping},
        {kXboxOneSUpdatedBT, &kAxisQuirkMapping},
        // These mappings are inferred to be the same based on the gamepad api
        // mappings.
        // See:
        // https://source.chromium.org/chromium/chromium/src/+/refs/heads/main:device/gamepad/gamepad_standard_mappings_linux.cc;l=968
        {kXboxAdaptiveBT, &kAxisQuirkMapping},
        {kXboxElite2BT, &kAxisQuirkMapping},
};

// Note: the majority of protocol errors are treated as non-fatal, and
// are intended to be handled gracefully, as is removal at any
// state of construction or operation. We should expect that
// 'sudden removal' can happen at any time, due to hotplugging
// or unexpected state changes from the wayland server.

static void sl_internal_gamepad_removed(void* data,
                                        struct zcr_gamepad_v2* gamepad) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_removed");
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  assert(host_gamepad->state == kStatePending ||
         host_gamepad->state == kStateActivated ||
         host_gamepad->state == kStateError);

  if (host_gamepad->uinput_dev != NULL)
    Libevdev::Get()->uinput_destroy(host_gamepad->uinput_dev);
  if (host_gamepad->ev_dev != NULL)
    Libevdev::Get()->free(host_gamepad->ev_dev);

  zcr_gamepad_v2_destroy(gamepad);

  wl_list_remove(&host_gamepad->link);
  delete host_gamepad;
}

// Helper function to remap the input events from a host_gamepad into the
// correct output event to be emulated by the generated uinput device.
static bool remap_input(struct sl_host_gamepad* host_gamepad, uint32_t& input) {
  if (host_gamepad->mapping == nullptr) {
    return true;
  }
  auto it = host_gamepad->mapping->find(input);
  if (it != host_gamepad->mapping->end()) {
    input = it->second;
    return true;
  }
  // If a mapping exists, and we get an input we don't expect
  // or don't handle, we shouldn't emulate it. An example of this
  // is that the DualSense controller's triggers activate an axis
  // and a button at the same time, which would result in unexpected
  // behaviour if we forwarded both inputs.
  return false;
}

static void sl_internal_gamepad_axis(void* data,
                                     struct zcr_gamepad_v2* gamepad,
                                     uint32_t time,
                                     uint32_t axis,
                                     wl_fixed_t value) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_axis");
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStateActivated)
    return;

  if (!remap_input(host_gamepad, axis))
    return;

  // Note: incoming time is ignored, it will be regenerated from current time.
  Libevdev::Get()->uinput_write_event(host_gamepad->uinput_dev, EV_ABS, axis,
                                      wl_fixed_to_double(value));
}

static void sl_internal_gamepad_button(void* data,
                                       struct zcr_gamepad_v2* gamepad,
                                       uint32_t time,
                                       uint32_t button,
                                       uint32_t state,
                                       wl_fixed_t analog) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_button");
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStateActivated)
    return;

  if (!remap_input(host_gamepad, button))
    return;

  // Note: Exo wayland server always sends analog==0, only pay attention
  // to state.
  int value = (state == ZCR_GAMEPAD_V2_BUTTON_STATE_PRESSED) ? 1 : 0;

  // Note: incoming time is ignored, it will be regenerated from current time.
  Libevdev::Get()->uinput_write_event(host_gamepad->uinput_dev, EV_KEY, button,
                                      value);
}

static void sl_internal_gamepad_frame(void* data,
                                      struct zcr_gamepad_v2* gamepad,
                                      uint32_t time) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_frame");
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStateActivated)
    return;

  // Note: incoming time is ignored, it will be regenerated from current time.
  Libevdev::Get()->uinput_write_event(host_gamepad->uinput_dev, EV_SYN,
                                      SYN_REPORT, 0);
}

static void sl_internal_gamepad_axis_added(void* data,
                                           struct zcr_gamepad_v2* gamepad,
                                           uint32_t index,
                                           int32_t min_value,
                                           int32_t max_value,
                                           int32_t flat,
                                           int32_t fuzz,
                                           int32_t resolution) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_axis_added");
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;
  struct input_absinfo info = {.value = 0,  // Does this matter?
                               .minimum = min_value,
                               .maximum = max_value,
                               .fuzz = fuzz,
                               .flat = flat,
                               .resolution = resolution};

  if (host_gamepad->state != kStatePending) {
    fprintf(stderr, "error: %s invoked in unexpected state %d\n", __func__,
            host_gamepad->state);
    host_gamepad->state = kStateError;
    return;
  }

  if (!remap_input(host_gamepad, index))
    return;

  Libevdev::Get()->enable_event_code(host_gamepad->ev_dev, EV_ABS, index,
                                     &info);
}

static void sl_internal_gamepad_activated(void* data,
                                          struct zcr_gamepad_v2* gamepad) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_activated");
  struct sl_host_gamepad* host_gamepad = (struct sl_host_gamepad*)data;

  if (host_gamepad->state != kStatePending) {
    fprintf(stderr, "error: %s invoked in unexpected state %d\n", __func__,
            host_gamepad->state);
    host_gamepad->state = kStateError;
    return;
  }

  int err = Libevdev::Get()->uinput_create_from_device(
      host_gamepad->ev_dev, LIBEVDEV_UINPUT_OPEN_MANAGED,
      &host_gamepad->uinput_dev);
  if (err == 0) {
    // TODO(kenalba): can we destroy and clean up the ev_dev now?
    host_gamepad->state = kStateActivated;
  } else {
    fprintf(stderr,
            "error: libevdev_uinput_create_from_device failed with error %d\n",
            err);
    host_gamepad->state = kStateError;
  }
}

static void sl_internal_gamepad_vibrator_added(
    void* data,
    struct zcr_gamepad_v2* gamepad,
    struct zcr_gamepad_vibrator_v2* vibrator) {
  TRACE_EVENT("gaming", "sl_internal_gamepad_vibrator_added");
  // TODO(kenalba): add vibration logic
}

static const struct zcr_gamepad_v2_listener sl_internal_gamepad_listener = {
    sl_internal_gamepad_removed,       sl_internal_gamepad_axis,
    sl_internal_gamepad_button,        sl_internal_gamepad_frame,
    sl_internal_gamepad_axis_added,    sl_internal_gamepad_activated,
    sl_internal_gamepad_vibrator_added};

static void sl_internal_gaming_seat_gamepad_added_with_device_info(
    void* data,
    struct zcr_gaming_seat_v2* gaming_seat,
    struct zcr_gamepad_v2* gamepad,
    const char* name,
    uint32_t bus,
    uint32_t vendor_id,
    uint32_t product_id,
    uint32_t version) {
  TRACE_EVENT("gaming",
              "sl_internal_gaming_seat_gamepad_added_with_device_info");
  struct sl_context* ctx = (struct sl_context*)data;
  struct sl_host_gamepad* host_gamepad = new sl_host_gamepad();
  wl_list_insert(&ctx->gamepads, &host_gamepad->link);
  zcr_gamepad_v2_add_listener(gamepad, &sl_internal_gamepad_listener,
                              host_gamepad);

  host_gamepad->ctx = ctx;
  host_gamepad->state = kStatePending;
  host_gamepad->ev_dev = Libevdev::Get()->new_evdev();
  host_gamepad->uinput_dev = NULL;
  host_gamepad->mapping = nullptr;

  if (host_gamepad->ev_dev == NULL) {
    fprintf(stderr, "error: libevdev_new failed\n");
    host_gamepad->state = kStateError;
    return;
  }

  // We provide limited remapping at this time. Only moderately XBox360
  // HID compatible controllers are likely to work well.
  auto it = kDeviceMappings.find(DeviceID{vendor_id, product_id, version});
  if (it != kDeviceMappings.end()) {
    host_gamepad->mapping = it->second;
  }

  // Describe a common controller
  Libevdev::Get()->set_name(host_gamepad->ev_dev, kXboxName);
  Libevdev::Get()->set_id_bustype(host_gamepad->ev_dev, kUsbBus);
  Libevdev::Get()->set_id_vendor(host_gamepad->ev_dev, kXboxVendor);
  Libevdev::Get()->set_id_product(host_gamepad->ev_dev, kXboxProduct);
  Libevdev::Get()->set_id_version(host_gamepad->ev_dev, kXboxVersion);

  // Enable common set of buttons
  for (unsigned int i = 0; i < ARRAY_SIZE(kButtons); i++)
    Libevdev::Get()->enable_event_code(host_gamepad->ev_dev, EV_KEY,
                                       kButtons[i], NULL);
}  // NOLINT(whitespace/indent), lint bug b/173143790

// Note: not currently implemented by Exo.
static void sl_internal_gaming_seat_gamepad_added(
    void* data,
    struct zcr_gaming_seat_v2* gaming_seat,
    struct zcr_gamepad_v2* gamepad) {
  TRACE_EVENT("gaming", "sl_internal_gaming_seat_gamepad_added");
  fprintf(stderr,
          "error: sl_internal_gaming_seat_gamepad_added unimplemented\n");
}

static const struct zcr_gaming_seat_v2_listener
    sl_internal_gaming_seat_listener = {
        sl_internal_gaming_seat_gamepad_added,
        sl_internal_gaming_seat_gamepad_added_with_device_info};

void sl_gaming_seat_add_listener(struct sl_context* ctx) {
  if (ctx->gaming_input_manager && ctx->gaming_input_manager->internal) {
    TRACE_EVENT("gaming", "sl_gaming_seat_add_listener");
    ctx->gaming_seat = zcr_gaming_input_v2_get_gaming_seat(
        ctx->gaming_input_manager->internal, ctx->default_seat->proxy);
    zcr_gaming_seat_v2_add_listener(ctx->gaming_seat,
                                    &sl_internal_gaming_seat_listener, ctx);
  }
}
