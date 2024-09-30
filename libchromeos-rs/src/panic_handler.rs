// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A panic handler for better crash signatures for rust apps.

use nix::sys::memfd::memfd_create;
use nix::sys::memfd::MemFdCreateFlag;
use std::borrow::Cow;
use std::ffi::CString;
use std::fs::File;
use std::io::Result;
use std::io::Write;
use std::mem;
use std::os::unix::prelude::FromRawFd;
use std::panic;
use std::process::abort;

const PANIC_MEMFD_NAME: &str = "RUST_PANIC_SIG";

fn create_panic_memfd() -> nix::Result<File> {
    let fd: i32 = memfd_create(
        &CString::new(PANIC_MEMFD_NAME).unwrap(),
        MemFdCreateFlag::MFD_CLOEXEC | MemFdCreateFlag::MFD_ALLOW_SEALING,
    )?;
    // SAFETY: Safe since the fd is newly created and not owned yet. `File` will own the fd.
    Ok(unsafe { File::from_raw_fd(fd) })
}

#[rustversion::since(1.81)]
type PanicHookInfo<'a> = panic::PanicHookInfo<'a>;

#[rustversion::before(1.81)]
type PanicHookInfo<'a> = panic::PanicInfo<'a>;

fn panic_info_payload_str_common<'a>(panic_info: &'a PanicHookInfo<'_>) -> Option<&'a str> {
    let payload = panic_info.payload();
    payload
        .downcast_ref::<&str>()
        .copied()
        .or_else(|| payload.downcast_ref::<String>().map(|x| x.as_str()))
}

#[rustversion::since(1.81)]
fn panic_info_payload_str<'a>(panic_info: &'a PanicHookInfo<'_>) -> Option<&'a str> {
    panic_info_payload_str_common(panic_info)
}

#[rustversion::before(1.81)]
fn panic_info_payload_str<'a>(panic_info: &'a PanicHookInfo<'_>) -> Option<Cow<'a, str>> {
    if let Some(message) = panic_info.message() {
        Some(Cow::Owned(format!("{}", message)))
    } else {
        panic_info_payload_str_common(panic_info).map(Cow::Borrowed)
    }
}

// TODO(b/309651697): This was written to be compatible with existing PanicHookInfo formatting, but
// it should probably be made more stable.
fn format_panic_info<W: Write>(w: &mut W, panic_info: &PanicHookInfo<'_>) -> Result<()> {
    write!(w, "panicked at '")?;

    if let Some(message) = panic_info_payload_str(panic_info) {
        write!(w, "{}", message)?;
    }
    write!(w, "', ")?;

    // At the time of writing, `PanicHookInfo::location()` cannot return `None`.
    match panic_info.location() {
        Some(location) => {
            write!(w, "{}", location)?;
        }
        None => {
            write!(w, "no location info")?;
        }
    }
    Ok(())
}

/// Inserts a panic handler that writes the panic info to a memfd called
/// "RUST_PANIC_SIG" before calling the original panic handler. This
/// makes it possible for external crash handlers to recover the panic info.
pub fn install_memfd_handler() {
    let hook = panic::take_hook();
    panic::set_hook(Box::new(move |p| {
        // On failure, ignore the error and call the original handler.
        if let Ok(mut panic_memfd) = create_panic_memfd() {
            let _ = format_panic_info(&mut panic_memfd, p);
            // Intentionally leak panic_memfd so it is picked up by the crash handler.
            mem::forget(panic_memfd);
        }
        hook(p);

        // If this is a multithreaded program, a panic in one thread will not kill the whole
        // process. Abort so the entire process gets killed and produces a core dump.
        abort();
    }));
}
