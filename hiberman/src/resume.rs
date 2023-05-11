// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements hibernate resume functionality.

use std::fs;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Read;
use std::mem;
use std::time::Duration;
use std::time::Instant;
use std::time::UNIX_EPOCH;

use anyhow::Context;
use anyhow::Result;
use libchromeos::secure_blob::SecureBlob;
use log::debug;
use log::error;
use log::info;
use log::warn;

use crate::cookie::cookie_description;
use crate::cookie::get_hibernate_cookie;
use crate::cookie::set_hibernate_cookie;
use crate::cookie::HibernateCookieValue;
use crate::device_mapper::DeviceMapper;
use crate::files::remove_resume_in_progress_file;
use crate::hiberlog;
use crate::hiberlog::redirect_log;
use crate::hiberlog::redirect_log_to_file;
use crate::hiberlog::replay_logs;
use crate::hiberlog::HiberlogOut;
use crate::hiberutil::lock_process_memory;
use crate::hiberutil::path_to_stateful_block;
use crate::hiberutil::HibernateError;
use crate::hiberutil::HibernateStage;
use crate::hiberutil::ResumeOptions;
use crate::hiberutil::TimestampFile;
use crate::lvm::activate_physical_lv;
use crate::metrics::read_and_send_metrics;
use crate::metrics::METRICS_LOGGER;
use crate::powerd::PowerdPendingResume;
use crate::resume_dbus::{get_user_key, wait_for_resume_dbus_event, ResumeRequest};
use crate::snapdev::FrozenUserspaceTicket;
use crate::snapdev::SnapshotDevice;
use crate::snapdev::SnapshotMode;
use crate::volume::ActiveMount;
use crate::volume::PendingStatefulMerge;
use crate::volume::VolumeManager;

/// The expected size of a TPM key.
const TPM_SEED_SIZE: usize = 32;
/// The path where the TPM key will be stored.
const TPM_SEED_FILE: &str = "/run/hiberman/tpm_seed";

/// The ResumeConductor orchestrates the various individual instruments that
/// work in concert to resume the system from hibernation.
pub struct ResumeConductor {
    options: ResumeOptions,
    stateful_block_path: String,
    timestamp_start: Duration,
}

impl ResumeConductor {
    /// Create a new resume conductor in prepration for an impending resume.
    pub fn new() -> Result<Self> {
        Ok(ResumeConductor {
            options: Default::default(),
            stateful_block_path: path_to_stateful_block()?,
            timestamp_start: Duration::ZERO,
        })
    }

    /// Public entry point into the resume process. In the case of a successful
    /// resume, this does not return, as the resume image is running instead. In
    /// the case of resume failure, an error is returned.
    pub fn resume(&mut self, options: ResumeOptions) -> Result<()> {
        info!("Beginning resume");
        // Ensure the persistent version of the stateful block device is available.
        let _rw_stateful_lv = activate_physical_lv("unencrypted")?;
        self.options = options;
        // Create a variable that will merge the stateful snapshots when this
        // function returns one way or another.
        let mut volume_manager = VolumeManager::new()?;
        let pending_merge = PendingStatefulMerge::new(&mut volume_manager)?;
        // Start keeping logs in memory, anticipating success.
        redirect_log(HiberlogOut::BufferInMemory);

        let result = self.resume_inner();

        // If we get here resuming from hibernate failed and we continue to
        // run the bootstrap system.

        // Move pending and future logs to syslog.
        redirect_log(HiberlogOut::Syslog);

        // Mount hibermeta for access to logs and metrics. Create it if it doesn't exist yet.
        let _hibermeta_mount = VolumeManager::new()?.setup_hibermeta_lv(true)?;

        // Now replay earlier logs. Don't wipe the logs out if this is just a dry
        // run.
        replay_logs(true, !self.options.dry_run);
        // Remove the resume_in_progress token file if it exists.
        remove_resume_in_progress_file();
        // Since resume_inner() returned, we are no longer in a viable resume
        // path. Drop the pending merge object, causing the stateful
        // dm-snapshots to merge with their origins.
        drop(pending_merge);
        // Read the metrics files to send out samples.
        read_and_send_metrics();

        result
    }

    /// Helper function to perform the meat of the resume action now that the
    /// logging is routed.
    fn resume_inner(&mut self) -> Result<()> {
        let mut volume_manager = VolumeManager::new()?;

        // If resume succeeds, resume_inner doesn't return. Leaving resume_inner indicates resume
        // failure or resume abortion. The D-Bus method waits on completion_receiver until
        // resume_inner returns and _completion_sender is dropped.
        let (_completion_sender, completion_receiver) = crossbeam_channel::bounded::<()>(0);

        if let Err(e) = self.decide_to_resume() {
            // No resume from hibernate

            // Make sure the thinpool is writable before removing the LVs.
            volume_manager.make_thinpool_rw()?;

            // Remove hiberimage volumes if they exist to release allocated
            // storage to the thinpool.
            volume_manager.teardown_hiberimage()?;

            // Set up the snapshot device for future hibernates
            self.setup_snapshot_device(true, completion_receiver)?;

            volume_manager.lockdown_hiberimage()?;

            return Err(e);
        }

        let hibermeta_mount = volume_manager.setup_hibermeta_lv(false)?;

        // Set up the snapshot device for resuming
        self.setup_snapshot_device(false, completion_receiver)?;

        debug!("Opening hiberimage");
        let hiber_image_file = OpenOptions::new()
            .read(true)
            .create(false)
            .open(DeviceMapper::device_path(VolumeManager::HIBERIMAGE).unwrap())
            .unwrap();

        volume_manager.lockdown_hiberimage()?;

        let _locked_memory = lock_process_memory()?;
        self.resume_system(hiber_image_file, hibermeta_mount)
    }

    /// Helper function to evaluate the hibernate cookie and decide whether or
    /// not to continue with resume.
    fn decide_to_resume(&self) -> Result<()> {
        // If the cookie left by hibernate and updated by resume-init doesn't
        // indicate readiness, skip the resume unless testing manually.
        let cookie = get_hibernate_cookie(Some(&self.stateful_block_path))
            .context("Failed to get hibernate cookie")?;
        let description = cookie_description(&cookie);

        if cookie == HibernateCookieValue::ResumeInProgress || self.options.dry_run {
            if cookie != HibernateCookieValue::ResumeInProgress {
                info!(
                    "Hibernate cookie was {}, continuing anyway due to --dry-run",
                    description
                );
            }

            return Ok(());
        } else if cookie == HibernateCookieValue::NoResume {
            info!("No resume pending");

            return Err(HibernateError::CookieError("No resume pending".to_string()).into());
        }

        warn!("Hibernate cookie was {}, abandoning resume", description);

        // If the cookie indicates an emergency reboot, clear it back to
        // nothing, as the problem was logged.
        if cookie == HibernateCookieValue::EmergencyReboot {
            set_hibernate_cookie(
                Some(&self.stateful_block_path),
                HibernateCookieValue::NoResume,
            )
            .context("Failed to clear emergency reboot cookie")?;
        }

        Err(HibernateError::CookieError(format!(
            "Cookie was {}, abandoning resume",
            description
        )))
        .context("Aborting resume due to cookie")
    }

    /// Inner helper function to read the resume image and launch it.
    fn resume_system(
        &mut self,
        hiber_image_file: File,
        mut hibermeta_mount: ActiveMount,
    ) -> Result<()> {
        let log_file_path = hiberlog::LogFile::get_path(HibernateStage::Resume);
        let log_file = hiberlog::LogFile::create(log_file_path)?;
        // Start logging to the resume logger.
        let redirect_guard = redirect_log_to_file(log_file);

        let mut snap_dev = SnapshotDevice::new(SnapshotMode::Write)?;

        let start = Instant::now();
        // Load the snapshot image into the kernel
        let image_size = snap_dev.load_image(hiber_image_file)?;

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            metrics_logger.metrics_send_io_sample("ReadMainImage", image_size, start.elapsed());
        }

        // Let other daemons know it's the end of the world.
        let _powerd_resume =
            PowerdPendingResume::new().context("Failed to call powerd for imminent resume")?;
        // Write a tombstone indicating we got basically all the way through to
        // attempting the resume. Both the current value (ResumeInProgress) and
        // this ResumeAborting value cause a reboot-after-crash to do the right
        // thing.
        set_hibernate_cookie(
            Some(&self.stateful_block_path),
            HibernateCookieValue::ResumeAborting,
        )
        .context("Failed to set hibernate cookie to ResumeAborting")?;

        info!("Freezing userspace");
        let frozen_userspace = snap_dev.freeze_userspace()?;

        TimestampFile::record_timestamp("resume_start.ts", &self.timestamp_start)?;

        let prep_time = UNIX_EPOCH.elapsed()? - self.timestamp_start;
        debug!(
            "Preparation for resume from hibernate took {}.{}.s",
            prep_time.as_secs(),
            prep_time.subsec_millis()
        );
        // TODO: log metric?

        {
            let mut metrics_logger = METRICS_LOGGER.lock().unwrap();
            // Flush the metrics file before unmounting 'hibermeta'.
            metrics_logger.flush()?;
        }

        // Keep logs in memory for now.
        mem::drop(redirect_guard);

        hibermeta_mount.unmount()?;

        // This is safe because sync() does not modify memory.
        unsafe {
            libc::sync();
        }

        if self.options.dry_run {
            info!("Not launching resume image: in a dry run.");

            Ok(())
        } else {
            self.launch_resume_image(frozen_userspace)
        }
    }

    /// Helper to set up the 'hiberimage' DM device and configuring it as
    /// snapshot device for hibernate.
    ///
    /// # Arguments
    ///
    /// * `new_hiberimage` - Indicates whether to create a new hiberimage or
    ///                      use an existing one (for resuming).
    /// * `completion_receiver` - Used to wait for resume completion.
    fn setup_snapshot_device(
        &mut self,
        new_hiberimage: bool,
        completion_receiver: crossbeam_channel::Receiver<()>,
    ) -> Result<()> {
        // Load the TPM derived key.
        let tpm_key: SecureBlob = self.get_tpm_derived_integrity_key()?;

        let user_key = match wait_for_resume_dbus_event(completion_receiver)? {
            ResumeRequest::ResumeAccountId { account_id } => get_user_key(&account_id, &[])?,
            ResumeRequest::ResumeAuthSessionId { auth_session_id } => {
                get_user_key("", &auth_session_id)?
            }
            ResumeRequest::Abort { reason } => {
                // Abort resume.
                info!("Aborting resume: {:?}", reason);
                return Ok(());
            }
        };

        self.timestamp_start = UNIX_EPOCH.elapsed().unwrap();

        VolumeManager::new()?.setup_hiberimage(
            tpm_key.as_ref(),
            user_key.as_ref(),
            new_hiberimage,
        )?;

        SnapshotDevice::new(SnapshotMode::Read)?
            .set_block_device(&DeviceMapper::device_path(VolumeManager::HIBERIMAGE).unwrap())
    }

    fn get_tpm_derived_integrity_key(&self) -> Result<SecureBlob> {
        let mut f = File::open(TPM_SEED_FILE)?;

        // Now that we have the file open, immediately unlink it.
        fs::remove_file(TPM_SEED_FILE)?;

        let mut buf = Vec::new();
        f.read_to_end(&mut buf)?;
        if buf.len() != TPM_SEED_SIZE {
            return Err(HibernateError::KeyRetrievalError()).context("Incorrect size for tpm_seed");
        }

        Ok(SecureBlob::from(buf))
    }

    /// Jump into the already-loaded resume image. The PendingResumeCall isn't
    /// actually used, but is received to enforce the lifetime of the object.
    /// This prevents bugs where it accidentally gets dropped by the caller too
    /// soon, allowing normal boot to proceed while resume is also in progress.
    fn launch_resume_image(&mut self, mut frozen_userspace: FrozenUserspaceTicket) -> Result<()> {
        // Jump into the restore image. This resumes execution in the lower
        // portion of suspend_system() on success. Flush and stop the logging
        // before control is lost.
        info!("Launching resume image");
        let snap_dev = frozen_userspace.as_mut();
        let result = snap_dev.atomic_restore();
        error!("Resume failed");
        result
    }
}
