// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the boilerplate implementation of the IAllocator HAL interface,
// generated by the hidl-gen tool and then modified for use on Chrome OS.
// Modifications include:
// - Removal of non boiler plate client and server related code.
// - Reformatting to meet the Chrome OS coding standards.
//
// Originally generated with the command:
// $ hidl-gen -o output -L c++ -r android.hardware:hardware/interfaces \
//   android.hardware.neuralnetworks@1.1

#define LOG_TAG "android.hardware.neuralnetworks@1.1::Device"

#include <android/hardware/neuralnetworks/1.1/IDevice.h>
#include <hidl/Status.h>

namespace android {
namespace hardware {
namespace neuralnetworks {
namespace V1_1 {

const char* IDevice::descriptor("android.hardware.neuralnetworks@1.1::IDevice");

::android::hardware::Return<void> IDevice::interfaceChain(
    interfaceChain_cb _hidl_cb) {
  _hidl_cb({
      ::android::hardware::neuralnetworks::V1_1::IDevice::descriptor,
      ::android::hardware::neuralnetworks::V1_0::IDevice::descriptor,
      ::android::hidl::base::V1_0::IBase::descriptor,
  });
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IDevice::debug(
    const ::android::hardware::hidl_handle& fd,
    const ::android::hardware::hidl_vec<::android::hardware::hidl_string>&
        options) {
  (void)fd;
  (void)options;
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IDevice::interfaceDescriptor(
    interfaceDescriptor_cb _hidl_cb) {
  _hidl_cb(::android::hardware::neuralnetworks::V1_1::IDevice::descriptor);
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IDevice::getHashChain(
    getHashChain_cb _hidl_cb) {
  _hidl_cb(
      {/* 7698dc2382a2eeb43541840e3ee624f34108efdfb976b2bfa7c13ef15fb8c4c4 */
       (uint8_t[32]){118, 152, 220, 35,  130, 162, 238, 180, 53,  65,  132,
                     14,  62,  230, 36,  243, 65,  8,   239, 223, 185, 118,
                     178, 191, 167, 193, 62,  241, 95,  184, 196, 196},
       /* 5804ca86611d72e5481f022b3a0c1b334217f2e4988dad25730c42af2d1f4d1c */
       (uint8_t[32]){88,  4,  202, 134, 97, 29,  114, 229, 72,  31,  2,
                     43,  58, 12,  27,  51, 66,  23,  242, 228, 152, 141,
                     173, 37, 115, 12,  66, 175, 45,  31,  77,  28},
       /* ec7fd79ed02dfa85bc499426adae3ebe23ef0524f3cd6957139324b83b18ca4c */
       (uint8_t[32]){236, 127, 215, 158, 208, 45,  250, 133, 188, 73,  148,
                     38,  173, 174, 62,  190, 35,  239, 5,   36,  243, 205,
                     105, 87,  19,  147, 36,  184, 59,  24,  202, 76}});
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IDevice::setHALInstrumentation() {
  return ::android::hardware::Void();
}

::android::hardware::Return<bool> IDevice::linkToDeath(
    const ::android::sp<::android::hardware::hidl_death_recipient>& recipient,
    uint64_t cookie) {
  (void)cookie;
  return (recipient != nullptr);
}

::android::hardware::Return<void> IDevice::ping() {
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IDevice::getDebugInfo(
    getDebugInfo_cb _hidl_cb) {
  ::android::hidl::base::V1_0::DebugInfo info = {};
  info.pid = -1;
  info.ptr = 0;
  info.arch =
#if defined(__LP64__)
      ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_64BIT;
#else
      ::android::hidl::base::V1_0::DebugInfo::Architecture::IS_32BIT;
#endif
  _hidl_cb(info);
  return ::android::hardware::Void();
}

::android::hardware::Return<void> IDevice::notifySyspropsChanged() {
  ::android::report_sysprop_change();
  return ::android::hardware::Void();
}

::android::hardware::Return<bool> IDevice::unlinkToDeath(
    const ::android::sp<::android::hardware::hidl_death_recipient>& recipient) {
  return (recipient != nullptr);
}

::android::hardware::Return<
    ::android::sp<::android::hardware::neuralnetworks::V1_1::IDevice>>
IDevice::castFrom(
    const ::android::sp<::android::hardware::neuralnetworks::V1_1::IDevice>&
        parent,
    bool /* emitError */) {
  return parent;
}

}  // namespace V1_1
}  // namespace neuralnetworks
}  // namespace hardware
}  // namespace android
