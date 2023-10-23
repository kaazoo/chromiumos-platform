// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// APIs to adjust the Quality of Service (QoS) expected for a thread or a
// process. QoS definitions map to performance characteristics.

use std::fs::write;
use std::io;

use anyhow::anyhow;
use anyhow::bail;
use anyhow::Context;
use anyhow::Result;
use procfs::process::Process;

// This is used in the test.
#[allow(dead_code)]
const NUM_PROCESS_STATES: usize = ProcessState::Background as usize + 1;
const NUM_THREAD_STATES: usize = ThreadState::Background as usize + 1;

/// Scheduler QoS states of a process.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ProcessState {
    Normal = 0,
    Background = 1,
}

impl TryFrom<u8> for ProcessState {
    type Error = ();

    fn try_from(v: u8) -> std::result::Result<Self, Self::Error> {
        match v {
            0 => Ok(Self::Normal),
            1 => Ok(Self::Background),
            _ => Err(()),
        }
    }
}

/// Scheduler QoS states of a thread.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ThreadState {
    UrgentBursty = 0,
    Urgent = 1,
    Balanced = 2,
    Eco = 3,
    Utility = 4,
    Background = 5,
}

impl TryFrom<u8> for ThreadState {
    type Error = ();

    fn try_from(v: u8) -> std::result::Result<Self, Self::Error> {
        match v {
            0 => Ok(Self::UrgentBursty),
            1 => Ok(Self::Urgent),
            2 => Ok(Self::Balanced),
            3 => Ok(Self::Eco),
            4 => Ok(Self::Utility),
            5 => Ok(Self::Background),
            _ => Err(()),
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum CpuSelection {
    All = 0,
    Efficient = 1,
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct sched_attr {
    pub size: u32,

    pub sched_policy: u32,
    pub sched_flags: u64,
    pub sched_nice: i32,

    pub sched_priority: u32,

    pub sched_runtime: u64,
    pub sched_deadline: u64,
    pub sched_period: u64,

    pub sched_util_min: u32,
    pub sched_util_max: u32,
}

const SCHED_FLAG_UTIL_CLAMP_MIN: u64 = 0x20;
const SCHED_FLAG_UTIL_CLAMP_MAX: u64 = 0x40;

impl sched_attr {
    pub const fn default() -> Self {
        Self {
            size: std::mem::size_of::<sched_attr>() as u32,
            sched_policy: libc::SCHED_OTHER as u32,
            sched_flags: 0,
            sched_nice: 0,
            sched_priority: 0,
            sched_runtime: 0,
            sched_deadline: 0,
            sched_period: 0,
            sched_util_min: 0,
            sched_util_max: UCLAMP_MAX,
        }
    }
}

/// Check the kernel support setting uclamp via sched_attr.
///
/// sched_util_min and sched_util_max were added to sched_attr from Linux kernel
/// v5.3 and guarded by CONFIG_UCLAMP_TASK flag.
fn check_uclamp_support() -> io::Result<bool> {
    let mut attr = sched_attr::default();

    // SAFETY: sched_getattr only modifies fields of attr.
    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_getattr,
            0, // current thread
            &mut attr as *mut sched_attr as usize,
            std::mem::size_of::<sched_attr>() as u32,
            0,
        )
    };

    if res < 0 {
        // sched_getattr must succeeds in most cases.
        //
        // * no ESRCH because this is inqury for this thread.
        // * no E2BIG nor EINVAL because sched_attr struct must be correct.
        //   Otherwise following sched_setattr fail anyway.
        //
        // Some environments (e.g. qemu-user) do not support sched_getattr(2)
        // and may fail as ENOSYS.
        return Err(io::Error::last_os_error());
    }

    attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX;

    // SAFETY: sched_setattr does not modify userspace memory.
    let res = unsafe {
        libc::syscall(
            libc::SYS_sched_setattr,
            0, // current thread
            &mut attr as *mut sched_attr as usize,
            0,
        )
    };

    if res < 0 {
        let err = io::Error::last_os_error();
        if err.raw_os_error() == Some(libc::EOPNOTSUPP) {
            Ok(false)
        } else {
            Err(err)
        }
    } else {
        Ok(true)
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
struct ThreadSettings {
    sched_settings: sched_attr,
    cpuset: CpuSelection,
    // On systems that use EAS, EAS will try to pack workloads onto non-idle
    // cpus first as long as there is capacity. However, if an idle cpu was
    // chosen it would reduce the latency.
    prefer_idle: bool,
}

const CGROUP_NORMAL: &str = "/sys/fs/cgroup/cpu/resourced/normal/cgroup.procs";
const CGROUP_BACKGROUND: &str = "/sys/fs/cgroup/cpu/resourced/background/cgroup.procs";

// Note these might be changed to resourced specific folders in the futre
const CPUSET_ALL: &str = "/sys/fs/cgroup/cpuset/chrome/urgent/tasks";
const CPUSET_EFFICIENT: &str = "/sys/fs/cgroup/cpuset/chrome/non-urgent/tasks";

const UCLAMP_MAX: u32 = 1024;
const UCLAMP_BOOST_PERCENT: u32 = 60;
const UCLAMP_BOOSTED_MIN: u32 = (UCLAMP_BOOST_PERCENT * UCLAMP_MAX + 50) / 100;

// Thread QoS settings table
const THREAD_SETTINGS: [ThreadSettings; NUM_THREAD_STATES] = [
    // UrgentBursty
    ThreadSettings {
        sched_settings: sched_attr {
            sched_policy: libc::SCHED_FIFO as u32,
            sched_priority: 8,
            sched_util_min: UCLAMP_BOOSTED_MIN,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::All,
        prefer_idle: true,
    },
    // Urgent
    ThreadSettings {
        sched_settings: sched_attr {
            sched_nice: -8,
            sched_util_min: UCLAMP_BOOSTED_MIN,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::All,
        prefer_idle: true,
    },
    // Balanced
    ThreadSettings {
        sched_settings: sched_attr {
            ..sched_attr::default()
        },
        cpuset: CpuSelection::All,
        prefer_idle: true,
    },
    // Eco
    ThreadSettings {
        sched_settings: sched_attr {
            ..sched_attr::default()
        },
        cpuset: CpuSelection::Efficient,
        prefer_idle: false,
    },
    // Utility
    ThreadSettings {
        sched_settings: sched_attr {
            sched_nice: 1,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::Efficient,
        prefer_idle: false,
    },
    // Background
    ThreadSettings {
        sched_settings: sched_attr {
            sched_nice: 10,
            ..sched_attr::default()
        },
        cpuset: CpuSelection::Efficient,
        prefer_idle: false,
    },
];

fn is_same_process(process_id: i32, thread_id: i32) -> Result<bool> {
    let proc =
        Process::new(thread_id).map_err(|e| anyhow!("Failed to find process, error: {}", e))?;

    let stat = proc
        .status()
        .map_err(|e| anyhow!("Failed to find process status, error: {}", e))?;

    Ok(stat.tgid == process_id)
}

pub struct SchedQosContext {
    uclamp_support: bool,
}

impl SchedQosContext {
    pub fn new() -> io::Result<Self> {
        Ok(Self {
            uclamp_support: check_uclamp_support()?,
        })
    }

    pub fn set_process_state(
        // TODO(kawasin): Make this mut to update internal state mapping.
        &self,
        process_id: i32,
        process_state: ProcessState,
    ) -> Result<()> {
        match process_state {
            ProcessState::Normal => write(CGROUP_NORMAL, process_id.to_string())
                .context(format!("Failed to write to {}", CGROUP_NORMAL))?,
            ProcessState::Background => write(CGROUP_BACKGROUND, process_id.to_string())
                .context(format!("Failed to write to {}", CGROUP_BACKGROUND))?,
        }

        Ok(())
    }

    pub fn set_thread_state(
        // TODO(kawasin): Make this mut to update internal state mapping.
        &self,
        process_id: i32,
        thread_id: i32,
        thread_state: ThreadState,
    ) -> Result<()> {
        // Validate thread_id is a thread of process_id
        if !is_same_process(process_id, thread_id)? {
            bail!("Thread does not belong to process");
        }

        let thread_settings = &THREAD_SETTINGS[thread_state as usize];
        let mut temp_sched_attr = thread_settings.sched_settings;

        // Setting SCHED_FLAG_UTIL_CLAMP_MIN or SCHED_FLAG_UTIL_CLAMP_MAX should
        // be avoided if kernel does not support uclamp. Otherwise
        // sched_setattr(2) fails as EOPNOTSUPP.
        if self.uclamp_support {
            temp_sched_attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN | SCHED_FLAG_UTIL_CLAMP_MAX;
        };

        let res = unsafe {
            libc::syscall(
                libc::SYS_sched_setattr,
                thread_id,
                &mut temp_sched_attr as *mut sched_attr as usize,
                0,
            )
        };
        if res < 0 {
            bail!(
                "Failed to set scheduler attributes, error={}",
                io::Error::last_os_error()
            );
        }

        // Apply the cpuset setting
        match thread_settings.cpuset {
            CpuSelection::All => write(CPUSET_ALL, thread_id.to_string())
                .context(format!("Failed to write to {}", CPUSET_ALL))?,
            CpuSelection::Efficient => write(CPUSET_EFFICIENT, thread_id.to_string())
                .context(format!("Failed to write to {}", CPUSET_EFFICIENT))?,
        };

        // Apply latency sensitive. Latency_sensitive will prefer idle cores.
        // This is a patch not yet in upstream(http://crrev/c/2981472)
        let latency_sensitive_file =
            format!("/proc/{}/task/{}/latency_sensitive", process_id, thread_id);

        if std::path::Path::new(&latency_sensitive_file).exists() {
            let value = if thread_settings.prefer_idle { 1 } else { 0 };
            write(&latency_sensitive_file, value.to_string())
                .context(format!("Failed to write to {}", latency_sensitive_file))?;
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_process_state_conversion() {
        for state in [ProcessState::Normal, ProcessState::Background] {
            assert_eq!(state, ProcessState::try_from(state as u8).unwrap());
        }

        assert!(ProcessState::try_from(NUM_PROCESS_STATES as u8).is_err());
    }

    #[test]
    fn test_thread_state_conversion() {
        for state in [
            ThreadState::UrgentBursty,
            ThreadState::Urgent,
            ThreadState::Balanced,
            ThreadState::Eco,
            ThreadState::Utility,
            ThreadState::Background,
        ] {
            assert_eq!(state, ThreadState::try_from(state as u8).unwrap());
        }

        assert!(ThreadState::try_from(NUM_THREAD_STATES as u8).is_err());
    }
}
