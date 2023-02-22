// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Pressure stall information (PSI) utilities.
///
/// PSI documentation: https://docs.kernel.org/accounting/psi.html
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use std::os::unix::fs::OpenOptionsExt;
use std::os::unix::io::AsRawFd;
use std::os::unix::prelude::RawFd;
use std::time::Duration;

use anyhow::{bail, Context, Result};
use tokio::io::unix::AsyncFd;
use tokio::time::timeout;

// Converts the libc error return to Result::Err.
fn cerr(t: libc::c_int) -> Result<libc::c_int> {
    match t {
        -1 => Err(std::io::Error::last_os_error().into()),
        _ => Ok(t),
    }
}

// Create an epoll fd waiting for PRI event on the input file.
// It's a workaround that tokio doesn't directly support waiting on high priority (PRI) data.
// Reference: https://github.com/tokio-rs/tokio/issues/4885
fn create_epoll_pri(file: &File) -> Result<AsyncFd<RawFd>> {
    let fd = cerr(unsafe { libc::epoll_create1(0) }).context("epoll_create1 returns error")?;

    let mut event = libc::epoll_event {
        events: libc::EPOLLPRI as u32,
        u64: 0_u64,
    };

    cerr(unsafe { libc::epoll_ctl(fd, libc::EPOLL_CTL_ADD, file.as_raw_fd(), &mut event) })
        .context("epoll_ctl returns error")?;

    AsyncFd::new(fd).context("Failed to Create AsyncFd")
}

/// Wait for PSI monitor event that memory stall time exceeded a certain threshold in recent time
/// window. Returns Ok(true) if the PSI monitor event is triggered. Returns Ok(false) when waiting
/// time exceeded `max_waiting_ms`.
///
/// # Arguments
///
/// * `stall_ms` - Memory stall time in millisecond to trigger the PSI monitor event.
/// * `window_ms` - Time window in millisecond to check the stall threshold.
/// * `min_waiting_ms` - Minimal waiting time in millisecond. Used to prevent too frequent
/// triggering.
/// * `max_waiting_ms` - Maximal waiting time in millisecond. Used to prevent indefinite waiting.
///
/// PSI monitor documentation: https://docs.kernel.org/accounting/psi.html#monitoring-for-pressure-thresholds
pub async fn wait_psi_monitor_memory_event(
    stall_ms: u64,
    window_ms: u64,
    min_waiting_ms: u64,
    max_waiting_ms: u64,
) -> Result<bool> {
    // Check the parameters.
    if stall_ms > window_ms {
        bail!("The stall time couldn't be larger than the time window.");
    }
    if min_waiting_ms > max_waiting_ms {
        bail!("The minimal waiting time couldn't be larger than the maximal waiting time.");
    }

    let mut monitor_fd = OpenOptions::new()
        .read(true)
        .write(true)
        .custom_flags(libc::O_NONBLOCK)
        .open("/proc/pressure/memory")
        .context("Failed to open /proc/pressure/memory")?;

    // The config shall be a C Style null terminated string.
    let monitor_config = format!("some {} {}\0", stall_ms * 1000, window_ms * 1000);
    let monitor_config_bytes = monitor_config.as_bytes();
    let monitor_config_size = monitor_config_bytes.len();

    match monitor_fd.write(monitor_config_bytes) {
        Ok(write_size) => {
            // Writing monitor config string to /proc/pressure/memory doesn't trigger disk io, it
            // should be fully written in 1 syscall.
            if write_size != monitor_config_size {
                bail!(
                    "Write psi memory file size: {} != monitor config size: {}",
                    write_size,
                    monitor_config_size
                );
            }
        }
        Err(err) => {
            bail!("Write psi memory file error: {}", err);
        }
    }

    let async_fd = create_epoll_pri(&monitor_fd).context("create_epoll_pri returns error")?;

    tokio::time::sleep(Duration::from_millis(min_waiting_ms)).await;

    match timeout(
        Duration::from_millis(max_waiting_ms - min_waiting_ms),
        async_fd.readable(),
    )
    .await
    {
        Ok(_guard) => Ok(true), // Got psi monitor event.
        Err(_) => Ok(false),    // Wait for psi monitor timed out.
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_wait_psi_monitor_memory_event() {
        const MIN_WAITING_MS: u64 = 500;
        const MAX_WAITING_MS: u64 = 10000;
        const STALL_MS: u64 = 150;
        const WINDOW_MS: u64 = 1000;

        const WRONG_STALL_MS: u64 = 1001;
        const WRONG_MIN_WAITING_MS: u64 = 10001;

        // It should return error when stall is larger than window.
        assert!(wait_psi_monitor_memory_event(
            WRONG_STALL_MS,
            WINDOW_MS,
            MIN_WAITING_MS,
            MAX_WAITING_MS
        )
        .await
        .is_err());

        // It should return error when min waiting is larger than max waiting.
        assert!(wait_psi_monitor_memory_event(
            STALL_MS,
            WINDOW_MS,
            WRONG_MIN_WAITING_MS,
            MAX_WAITING_MS
        )
        .await
        .is_err());
    }
}
