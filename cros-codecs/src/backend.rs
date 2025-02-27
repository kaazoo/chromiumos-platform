// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared code for codec backends.
//!
//! A backend is a provider of codec decoding or encoding, most likely hardware-accelerated like
//! VAAPI. This module contains backend-related code that is not tied to any particular codec and
//! can be shared between various parts of this crate.

#[cfg(any(test, fuzzing))]
pub(crate) mod dummy;
#[cfg(any(feature = "v4l2", feature = "v4l2-experimental"))]
pub mod v4l2;
#[cfg(feature = "vaapi")]
pub mod vaapi;
