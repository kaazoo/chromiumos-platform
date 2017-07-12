// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/service.h"

#include <cstdlib>
#include <string>

#include <libminijail.h>
#include <scoped_minijail.h>
#include <linux/capability.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <chaps/pkcs11/cryptoki.h>
#include <brillo/syslog_logging.h>
#include <dbus/dbus.h>
#include <glib.h>
#include <openssl/evp.h>

#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/platform.h"

// TODO(wad) This is a placeholder DBus service which allows
//           chrome-login (and anything else running as chronos)
//           to request to mount, unmount, or check if a mapper
//           device is mounted. This is very temporary but should
//           serve as a baseline for moving all the shell scripts
//           into C++.
//           We will need a "CheckKey" interface as well to simplify
//           offline authentication checks.

namespace env {
static const char* kAttestationBasedEnrollmentDataFile = "ABE_DATA_FILE";
}

namespace switches {
// Keeps std* open for debugging.
static const char *kNoCloseOnDaemonize = "noclose";
static const char *kNoLegacyMount = "nolegacymount";
static const char *kDirEncryption = "direncryption";
}  // namespace switches

namespace {

void EnterSandbox() {
  constexpr char kUserId[] = "cryptohome";
  constexpr char kGroupId[] = "cryptohome";

  ScopedMinijail jail(minijail_new());
  CHECK_EQ(0, minijail_change_user(jail.get(), kUserId));
  CHECK_EQ(0, minijail_change_group(jail.get(), kGroupId));
  // NOTE: We can possibly remove the CAP_DAC_OVERRIDE capability by giving
  //       the "cryptohome" user access to /var/run/tcsd.socket (by adding
  //       "cryptohome" to the "tss" group), but that might cause a problem
  //       on upgrade, as "root" currently owns files like
  //       * /mnt/stateful_partition/.tpm_owned
  //       * /mnt/stateful_partition/.tpm_status
  //       * /mnt/stateful_partition/.tpm_status.sum
  //       which may need to be written by cryptohomed.

  // Capabilities bitset: 0x20000f
  minijail_use_caps(jail.get(),
                    CAP_TO_MASK(CAP_SYS_ADMIN) | CAP_TO_MASK(CAP_CHOWN) |
                        CAP_TO_MASK(CAP_DAC_OVERRIDE) |
                        CAP_TO_MASK(CAP_DAC_READ_SEARCH) |
                        CAP_TO_MASK(CAP_FOWNER));

  minijail_namespace_ipc(jail.get());
  minijail_namespace_uts(jail.get());
  // NOTE: We should enable cgroups namespace. Currently it does not work on
  //       Linux <4.6 and will crash cryptohome.
  // minijail_namespace_cgroups(jail.get());

  // NOTE: We should add the net namespace. The only time cryptohome contacts
  //       the network is when the service is running as "Monolithic" and the
  //       "InitializeCastKey" method is called

  minijail_no_new_privs(jail.get());

  minijail_enter(jail.get());
}

std::string ReadAbeDataFileContents(cryptohome::Platform* platform) {
  std::string data;

  const char* abe_data_file =
      std::getenv(env::kAttestationBasedEnrollmentDataFile);
  if (!abe_data_file)
    return data;

  base::FilePath file_path(abe_data_file);
  if (!platform->ReadFileToString(file_path, &data))
    LOG(FATAL) << "Could not read attestation-based enterprise enrollment data"
                  " in: " << file_path.value();
  return data;
}
}  // namespace

int main(int argc, char **argv) {
  EnterSandbox();

  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);

  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  // Read the file before we daemonize so it can be deleted as soon as we exit.
  cryptohome::Platform platform;
  std::string abe_data = ReadAbeDataFileContents(&platform);

  // Allow the commands to be configurable.
  base::CommandLine *cl = base::CommandLine::ForCurrentProcess();
  int noclose = cl->HasSwitch(switches::kNoCloseOnDaemonize);
  bool nolegacymount = cl->HasSwitch(switches::kNoLegacyMount);
  bool direncryption = cl->HasSwitch(switches::kDirEncryption);
  PLOG_IF(FATAL, daemon(0, noclose) == -1) << "Failed to daemonize";

  // Setup threading. This needs to be called before other calls into glib and
  // before multiple threads are created that access dbus.
  dbus_threads_init_default();

  // Initialize OpenSSL.
  OpenSSL_add_all_algorithms();

  cryptohome::ScopedMetricsInitializer metrics_initializer;

  cryptohome::Service* service = cryptohome::Service::CreateDefault(abe_data);

  service->set_legacy_mount(!nolegacymount);
  service->set_force_ecryptfs(!direncryption);

  if (!service->Initialize()) {
    LOG(FATAL) << "Service initialization failed";
    return 1;
  }

  if (!service->Register(brillo::dbus::GetSystemBusConnection())) {
    LOG(FATAL) << "DBUS service registration failed";
    return 1;
  }

  if (!service->Run()) {
    LOG(FATAL) << "Service run failed.";
    return 1;
  }

  // If PKCS #11 was initialized, this will tear it down.
  C_Finalize(NULL);

  return 0;
}
