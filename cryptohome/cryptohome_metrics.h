// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_METRICS_H_
#define CRYPTOHOME_CRYPTOHOME_METRICS_H_

#include <string>
#include <string_view>

#include <base/files/file.h>
#include <base/time/time.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <libhwsec/error/tpm_retry_action.h>
#include <metrics/metrics_library.h>

#include "cryptohome/auth_blocks/auth_block_type.h"
#include "cryptohome/auth_factor/type.h"
#include "cryptohome/crypto_error.h"
#include "cryptohome/data_migrator/metrics.h"
#include "cryptohome/migration_type.h"

namespace cryptohome {

// The derivation types used in the implementations of AuthBlock class.
// Refer to cryptohome/docs/ for more details.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DerivationType : int {
  // Derive a high-entropy secret from the user's password using scrypt.
  kScryptBacked = 0,
  // Low-entropy secrets that need brute force protection are mapped to
  // high-entropy secrets that can be obtained via a rate-limited lookup
  // enforced by the TPM/GSC.
  kLowEntropyCredential = 1,
  // Protecting user data via signing cryptographic keys stored on hardware
  // tokens, rather than via passwords. The token needs to present a valid
  // signature for the generated challenge to unseal a secret seed value, which
  // is then used as a KDF passphrase for scrypt to derive the wrapping key.
  // The sealing/unsealing algorithm involves TPM/GSC capabilities for achieving
  // the security strength.
  kSignatureChallengeProtected = 2,
  // TPM/GSC and user passkey is used to derive the wrapping keys which are
  // sealed to PCR.
  kTpmBackedPcrBound = 3,
  // TPM/GSC and user passkey is used to derive the wrapping key.
  kTpmBackedNonPcrBound = 4,
  // Deprecated state - both TPM/GSC and scrypt is being used.
  kDoubleWrapped = 5,
  // Secret is generated on the device and later derived by Cryptohome Recovery
  // process using data stored on the device and by Recovery Mediator service.
  kCryptohomeRecovery = 6,
  // TPM/GSC and user passkey is used to derive the wrapping keys which are
  // sealed to PCR and ECC auth value.
  kTpmBackedEcc = 7,
  // Biometrics credentials are protected by a rate-limiting protocol between
  // GSC and the biometrics auth stack. The auth stack is trusted to perform
  // matching correctly and securely, but rate-limiting is guarded by GSC.
  // Biometrics auth stack and GSC each provides half of the secret to derive
  // the key.
  kBiometrics = 8,
  kDerivationTypeNumBuckets  // Must be the last entry.
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum CryptohomeErrorMetric {
  kTpmFail = 1,
  kTcsKeyLoadFailed = 2,
  kTpmDefendLockRunning = 3,
  kDecryptAttemptButTpmKeyMissing = 4,
  kDecryptAttemptButTpmNotOwned = 5,
  kDecryptAttemptButTpmNotAvailable = 6,
  kDecryptAttemptButTpmKeyMismatch = 7,
  kDecryptAttemptWithTpmKeyFailed = 8,
  kCannotLoadTpmSrk = 9,
  kCannotReadTpmSrkPublic = 10,
  kCannotLoadTpmKey = 11,
  kCannotReadTpmPublicKey = 12,
  kTpmBadKeyProperty = 13,
  kLoadPkcs11TokenFailed = 14,
  kEncryptWithTpmFailed = 15,
  kTssCommunicationFailure = 16,
  kTssInvalidHandle = 17,
  kBothTpmAndScryptWrappedKeyset = 18,
  kEphemeralCleanUpFailed = 19,
  kTpmOutOfMemory = 20,
  kCryptohomeErrorNumBuckets  // Must be the last entry.
};

// These values are used to get the right param to send to metrics
// server. Entries should not be renumbered without a corresponding change in
// kTimerHistogramParams.
enum TimerType {
  kPkcs11InitTimer = 0,
  kMountExTimer = 1,
  kMountGuestExTimer = 2,
  kPerformEphemeralMountTimer = 3,
  kPerformMountTimer = 4,
  kGenerateEccAuthValueTimer = 5,
  kAuthSessionAddAuthFactorVKTimer = 6,
  kAuthSessionAddAuthFactorUSSTimer = 7,
  kAuthSessionAuthenticateAuthFactorVKTimer = 8,
  kAuthSessionAuthenticateAuthFactorUSSTimer = 9,
  kAuthSessionUpdateAuthFactorVKTimer = 10,
  kAuthSessionUpdateAuthFactorUSSTimer = 11,
  kAuthSessionRemoveAuthFactorUSSTimer = 12,
  kCreatePersistentUserTimer = 13,
  kAuthSessionTotalLifetimeTimer = 14,
  kAuthSessionAuthenticatedLifetimeTimer = 15,
  kUSSPersistTimer = 16,
  kUSSLoadPersistedTimer = 17,
  kUSSMigrationTimer = 18,
  kVaultSetupTimer = 19,
  kSELinuxRelabelTimer = 20,
  kStoreUserPolicyTimer = 21,
  kLoadUserPolicyTimer = 22,
  kAuthSessionReplaceAuthFactorTimer = 23,
  kNumTimerTypes  // For the number of timer types.
};

// Struct for recording metrics on how long certain AuthSession operations take.
struct AuthSessionPerformanceTimer {
  TimerType type;
  base::TimeTicks start_time;
  std::optional<AuthBlockType> auth_block_type;

  explicit AuthSessionPerformanceTimer(TimerType init_type)
      : type(init_type), start_time(base::TimeTicks::Now()) {}
  AuthSessionPerformanceTimer(TimerType init_type,
                              AuthBlockType init_auth_block_type)
      : type(init_type),
        start_time(base::TimeTicks::Now()),
        auth_block_type(init_auth_block_type) {}
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationFailedPathType {
  kMigrationFailedUnderOther = 1,
  kMigrationFailedUnderAndroidOther = 2,
  kMigrationFailedUnderAndroidCache = 3,
  kMigrationFailedUnderDownloads = 4,
  kMigrationFailedUnderCache = 5,
  kMigrationFailedUnderGcache = 6,
  kMigrationFailedPathTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class HomedirEncryptionType {
  kEcryptfs = 1,
  kDircrypto = 2,
  kDmcrypt = 3,
  kHomedirEncryptionTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class DiskCleanupProgress {
  kEphemeralUserProfilesCleaned = 1,
  kBrowserCacheCleanedAboveTarget = 2,
  kGoogleDriveCacheCleanedAboveTarget = 3,
  kGoogleDriveCacheCleanedAboveMinimum = 4,
  kAndroidCacheCleanedAboveTarget = 5,
  kAndroidCacheCleanedAboveMinimum = 6,
  kWholeUserProfilesCleanedAboveTarget = 7,
  kWholeUserProfilesCleaned = 8,
  kNoUnmountedCryptohomes = 9,
  kCacheVaultsCleanedAboveTarget = 10,
  kCacheVaultsCleanedAboveMinimum = 11,
  kSomeEphemeralUserProfilesCleanedAboveTarget = 12,
  kSomeEphemeralUserProfilesCleaned = 13,
  kDaemonStoreCacheCleanedAboveTarget = 14,
  kDaemonStoreCacheCleanedAboveMinimum = 15,
  kDaemonStoreCacheMountedUsersCleanedAboveTarget = 16,
  kDaemonStoreCacheMountedUsersCleanedAboveMinimum = 17,
  kNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class LoginDiskCleanupProgress {
  kWholeUserProfilesCleanedAboveTarget = 1,
  kWholeUserProfilesCleaned = 2,
  kNoUnmountedCryptohomes = 3,
  kNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class DiskCleanupResult {
  kDiskCleanupSuccess = 1,
  kDiskCleanupError = 2,
  kDiskCleanupSkip = 3,
  kNumBuckets
};

// List of the possible results of attempting a mount operation using the
// out-of-process mount helper.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class OOPMountOperationResult {
  kSuccess = 0,
  kFailedToStart = 1,
  kFailedToWriteRequestProtobuf = 2,
  kHelperProcessTimedOut = 3,
  kFailedToReadResponseProtobuf = 4,
  kMaxValue = kFailedToReadResponseProtobuf
};

// List of the possible results of attempting an unmount/mount clean-up
// using the out-of-process mount helper.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class OOPMountCleanupResult {
  kSuccess = 0,
  kFailedToPoke = 1,
  kFailedToWait = 2,
  kFailedToKill = 3,
  kMaxValue = kFailedToKill
};

// List of possible results from migrating the files at ~/MyFiles to
// ~/MyFiles/Downloads. These values are persisted to logs. Entries should not
// be renumbered and numeric values should never be reused.
enum class DownloadsMigrationStatus {
  // The migration just finished successfully.
  kSuccess = 0,

  // The migration was previously done, but the xattr was left as "migrating".
  kFixXattr = 1,

  // Cannot set the xattr to "migrating".
  kCannotSetXattrToMigrating = 2,

  // Cannot move ~/Downloads to ~/MyFiles/Downloads.
  kCannotMoveToMyFiles = 6,

  // Cannot set the xattr to "migrated".
  kCannotSetXattrToMigrated = 7,

  // ~/MyFiles/Downloads is already marked as "migrated", but a new ~/Downloads
  // folder somehow reappeared.
  kReappeared = 8,

  // It looks like a newly created cryptohome. There is nothing to move, and the
  // xattr is set to "migrated".
  kSetXattrForNewCryptoHome = 9,

  // ~/MyFiles/Downloads is already marked as "migrated".
  kAlreadyMigrated = 10,

  // Maximum enum value. Must be updated if more values are added.
  kMaxValue = kAlreadyMigrated
};

// Various counts for ReportVaultKeysetMetrics.
struct VaultKeysetMetrics {
  int missing_key_data_count = 0;
  int empty_label_count = 0;
  int empty_label_le_cred_count = 0;
  int le_cred_count = 0;
  int untyped_count = 0;
  int password_count = 0;
  int smart_unlock_count = 0;
  int smartcard_count = 0;
  int fingerprint_count = 0;
  int kiosk_count = 0;
  int unclassified_count = 0;
};

// List of all the legacy code paths' usage we are tracking. This will enable us
// to further clean up the code in the future, should any of these code paths
// are found not being used.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class LegacyCodePathLocation {
  // When a new keyset is being added, Cryptohome checks to see if the keyset
  // that authorizes that add keyset action has a reset_seed.
  // The goal of this block was to support pin, when the older keyset didn't
  // have reset_seed. In the newer versions of keyset, by default, we store a
  // reset_seed.
  kGenerateResetSeedDuringAddKey = 0,
  kMaxValue = kGenerateResetSeedDuringAddKey
};

inline constexpr char kCryptohomeErrorPrefix[] = "Cryptohome";
inline constexpr char kCryptohomeErrorHashedStackSuffix[] = "HashedStack";
inline constexpr char kCryptohomeErrorLeafWithTPMSuffix[] = "LeafErrorWithTPM";
inline constexpr char kCryptohomeErrorDevCheckUnexpectedStateSuffix[] =
    "DevUnexpectedState";
inline constexpr char kCryptohomeErrorAllLocationsSuffix[] = "AllLocations";
inline constexpr char kCryptohomeErrorUssMigrationErrorBucket[] =
    "UssMigrationError";
inline constexpr char kCryptohomeErrorRecreateAuthFactorErrorBucket[] =
    "RecreateAuthFactorError";
inline constexpr char kCryptohomeErrorPrepareAuthFactorErrorBucket[] =
    "PrepareAuthFactorError";
inline constexpr char kCryptohomeErrorAddAuthFactorErrorBucket[] =
    "AddAuthFactorError";
inline constexpr char kCryptohomeErrorAuthenticateAuthFactorErrorBucket[] =
    "AuthenticateAuthFactorError";
inline constexpr char kCryptohomeErrorRemoveAuthFactorErrorBucket[] =
    "RemoveAuthFactorError";
inline constexpr char kCryptohomeErrorUpdateRecoverableKeyStoreErrorBucket[] =
    "UpdateRecoverableKeyStoreError";
inline constexpr char kCryptohomeErrorCreateRecoverableKeyStoreErrorBucket[] =
    "CreateRecoverableKeyStoreError";

// List of possible auth factor backing store configurations that a user can
// have. This is determined by whether a user's factors are stored in vault
// keysets or the USS.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class AuthFactorBackingStoreConfig {
  kEmpty = 0,            // User has no auth factors.
  kVaultKeyset = 1,      // All factors are stored in vault keysets.
  kUserSecretStash = 2,  // All factors are stored in the user secret stash.
  kMixed = 3,            // Factors are stoed in a mix of backings stores.
  kMaxValue = kMixed,
};

// List of errors from migrating a vault keyset to USS (or success=0). This enum
// should be updated with any new errors that can occur, along with enums.xml.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class VkToUssMigrationStatus {
  kSuccess = 0,                      // Migration succeeded with no errors.
  kFailedPersist = 1,                // Migration failed when persisting to USS.
  kFailedInput = 2,                  // Unable to construct an AuthInput.
  kFailedUssCreation = 3,            // Unable to construct USS.
  kFailedAddingMigrationSecret = 4,  // Unable to construct a migration secret.
  kFailedUssDecrypt = 5,             // Unable to decrypt USS.
  kFailedRecordingMigrated = 6,      // Unable to store migrated state.
  kMaxValue = kFailedRecordingMigrated,
};

// List of possible results of attempting to cleanup a backup keyset for a user
// with mixed USS-VaultKeyset(VK) configuration. Mixed configuration is expected
// to happen with PIN and password factors and enum values are defined based on
// this.
enum class BackupKeysetCleanupResult {
  kRemovedBackupPassword = 0,      // Removal of password backup VK succeeded.
  kRemovedBackupPin = 1,           // Removal of PIN backup VK succeeded.
  kRemovedBackupOtherType = 2,     // Removal of other type backup VK succeeded.
  kAddResetSecretFailed = 3,       // Adding reset_secret to USS failed.
  kGetValidKeysetFailed = 4,       // Decrypt or load of backup VK failed.
  kRemoveFileFailedPin = 5,        // Remove file failed for password type.
  kRemoveFileFailedPassword = 6,   // Remove file failed for PIN type.
  kRemoveFileFailedOtherType = 7,  // Remove file failed for other factor type.
  kMaxValue = kRemoveFileFailedOtherType,
};

// List of possible results of recoverable key store certificate list update
// attempts. Recorded whenever a certificate list is fetched and given to the
// provider. Entries should not be renumbered and numeric values should never be
// reused.
enum class BackendCertProviderUpdateCertResult {
  kUpdateSuccess = 0,       // Certificate list is updated successfully.
  kUpdateNotNeeded = 1,     // Certificate list doesn't need an update.
  kParseVersionFailed = 2,  // Failed to parse the certificate list version.
  kVerifyFailed = 3,   // Failed to verify the signature + certificate XMLs.
  kPersistFailed = 4,  // Failed to persist the updated certificate list.
  kMaxValue = kPersistFailed,
};

// List of possible results of recoverable key store certificate list parsing
// and verification. Recorded on each
// |VerifyAndParseRecoverableKeyStoreBackendCertXmls| call. Entries should not
// be renumbered and numeric values should never be reused.
enum class VerifyAndParseBackendCertResult {
  kSuccess = 0,
  // Failed to parse the signature XML file.
  kParseSignatureFailed = 1,
  // Failed to verify the certificate chain in the signature XML file.
  kVerifySignatureFailed = 2,
  // Failed to verify the certificate file's signature.
  kVerifyCertFileSignatureFailed = 3,
  // Failed to parse the certificate XML file.
  kParseCertFailed = 4,
  // Failed to verify the certificate chain in the certificate XML file.
  kVerifyCertFailed = 5,
  // Failed to encode the parsed certificate list.
  kEncodeCertFailed = 6,
  kMaxValue = kEncodeCertFailed,
};

// Initializes cryptohome metrics. If this is not called, all calls to Report*
// will have no effect.
void InitializeMetrics();

// Cleans up and returns cryptohome metrics to an uninitialized state.
void TearDownMetrics();

// Get metrics handler for external libraries, when available.
MetricsLibraryInterface* GetMetrics();

// Override the internally used MetricsLibrary for testing purpose.
void OverrideMetricsLibraryForTesting(MetricsLibraryInterface* lib);

// Reset the internally used MetricsLibrary for testing purpose. This is usually
// used with OverrideMetricsLibraryForTesting().
void ClearMetricsLibraryForTesting();

// The |error| value is reported to the "Cryptohome.Errors" enum histogram.
void ReportCryptohomeError(CryptohomeErrorMetric error);

// Cros events are translated to an enum and reported to the generic
// "Platform.CrOSEvent" enum histogram. The |event| string must be registered in
// metrics/metrics_library.cc:kCrosEventNames.
void ReportCrosEvent(const char* event);

// Starts a timer for the given |timer_type|.
void ReportTimerStart(TimerType timer_type);

// Stops a timer and reports in milliseconds. Timers are reported to the
// "Cryptohome.TimeTo*" histograms.
void ReportTimerStop(TimerType timer_type);

// Reports a timer length in milliseconds, duration is calculated by the time it
// is called minus the start_time of the reported timer.
void ReportTimerDuration(
    const AuthSessionPerformanceTimer* auth_session_performance_timer);

void ReportTimerDuration(const TimerType& timer_type,
                         base::TimeTicks start_time,
                         const std::string& parameter_string);

// Reports the result of credentials revocation for `auth_block_type` to the
// "Cryptohome.{AuthBlockType}.CredentialRevocationResult" histogram.
void ReportRevokeCredentialResult(AuthBlockType auth_block_type,
                                  hwsec::TPMRetryAction result);

// Reports number of deleted user profiles to the
// "Cryptohome.DeletedUserProfiles" histogram.
void ReportDeletedUserProfiles(int user_profile_count);

// Reports total time taken by HomeDirs::FreeDiskSpace cleanup (milliseconds) to
// the "Cryptohome.FreeDiskSpaceTotalTime" histogram.
void ReportFreeDiskSpaceTotalTime(int ms);

// Reports total space freed by HomeDirs::FreeDiskSpace (in MiB) to
// the "Cryptohome.FreeDiskSpaceTotalFreedInMb" histogram.
void ReportFreeDiskSpaceTotalFreedInMb(int mb);

// Reports the time between HomeDirs::FreeDiskSpace cleanup calls (seconds) to
// the "Cryptohome.TimeBetweenFreeDiskSpace" histogram.
void ReportTimeBetweenFreeDiskSpace(int s);

// Reports removed GCache size by cryptohome to the
// "Cryptohome.GCache.FreedDiskSpaceInMb" histogram.
void ReportFreedGCacheDiskSpaceInMb(int mb);

// Reports removed Daemon Store Cache size by cryptohome to the
// "Cryptohome.FreedDaemonStoreCacheDiskSpaceInMb" histogram.
void ReportFreedDaemonStoreCacheDiskSpaceInMb(int mb);

// Reports removed Daemon Store Cache size by cryptohome for mounted users to
// the "Cryptohome.FreedDaemonStoreCacheMountedUsersDiskSpaceInMb" histogram.
void ReportFreedDaemonStoreCacheMountedUsersDiskSpaceInMb(int mb);

// Reports removed Cache Vault size by cryptohome to the
// "Cryptohome.FreedCacheVaultDiskSpaceInMb" histogram.
void ReportFreedCacheVaultDiskSpaceInMb(int mb);

// Reports total time taken by HomeDirs::FreeDiskSpaceDuringLogin cleanup
// (milliseconds) to the "Cryptohome.LoginDiskCleanupTotalTime" histogram.
void ReportLoginDiskCleanupTotalTime(int ms);

// Reports total space freed by HomeDirs::FreeDiskSpaceDuringLogin (in MiB) to
// the "Cryptohome.FreeDiskSpaceDuringLoginTotalFreedInMb" histogram.
void ReportFreeDiskSpaceDuringLoginTotalFreedInMb(int mb);

// Reports which topmost priority was reached to fulfill a cleanup request
// to the "Cryptohome.DiskCleanupProgress" enum histogram.
void ReportDiskCleanupProgress(DiskCleanupProgress progress);

// Report if the automatic disk cleanup encountered an error to the
// "Cryptohome.DiskCleanupResult" enum histogram.
void ReportDiskCleanupResult(DiskCleanupResult result);

// Reports which topmost priority was reached to fulfill a cleanup request
// to the "Cryptohome.LoginDiskCleanupProgress" enum histogram.
void ReportLoginDiskCleanupProgress(LoginDiskCleanupProgress progress);

// Report if the automatic disk cleanup encountered an error to the
// "Cryptohome.LoginDiskCleanupResult" enum histogram.
void ReportLoginDiskCleanupResult(DiskCleanupResult result);

// Report the amount of free space available during login to the
// "Cryptohome.LoginDiskCleanupAvailableSpace" enum histogram.
void ReportLoginDiskCleanupAvailableSpace(int64_t space);

// The |type| value is reported to the "Cryptohome.HomedirEncryptionType" enum
// histogram.
void ReportHomedirEncryptionType(HomedirEncryptionType type);

// Reports the number of user directories present in the system.
void ReportNumUserHomeDirectories(int num_users);

// Reports the number of log entries attempted to replay during an LE log replay
// operation. This count is one-based, zero is used as a sentinel value for "all
// entries", reported when none of the log entries matches the root hash.
void ReportLELogReplayEntryCount(size_t entry_count);

// Reports the result of an out-of-process mount operation.
void ReportOOPMountOperationResult(OOPMountOperationResult result);

// Reports the result of an out-of-process cleanup operation.
void ReportOOPMountCleanupResult(OOPMountCleanupResult result);

// Reports the result of PrepareForRemoval() for `auth_block_type`
// to the "Cryptohome.{AuthBlockType}.PrepareForRemovalResult" histogram.
void ReportPrepareForRemovalResult(AuthBlockType auth_block_type,
                                   CryptoError result);

// Reports the result of a RestoreSELinuxContexts operation for /home/.shadow.
void ReportRestoreSELinuxContextResultForShadowDir(bool success);

// Reports the result of a RestoreSELinuxContexts operation for the bind mounted
// directories under user home directory.
void ReportRestoreSELinuxContextResultForHomeDir(bool success);

// Reports which kinds of auth block we are used to derive.
void ReportCreateAuthBlock(AuthBlockType type);

// Reports which kinds of auth block we are used to derive.
void ReportDeriveAuthBlock(AuthBlockType type);

// Reports which kinds of auth block we are used to select auth factor.
void ReportSelectFactorAuthBlock(AuthBlockType type);

// Reports which code paths are being used today and performing what actions.
void ReportUsageOfLegacyCodePath(LegacyCodePathLocation location, bool result);

// Reports certain metrics around VaultKeyset such as the number of empty
// labels, the number of smart unlock keys, number of password keys with and
// without KeyProviderData, and the number of labeled/label-less PIN
// VaultKeysets.
void ReportVaultKeysetMetrics(const VaultKeysetMetrics& keyset_metrics);

// Reports number of files that exist in ~/MyFiles/Downloads prior to migrating
// and bind mounting. This only records the top-level items but does not record
// items in sub-directories.
void ReportMaskedDownloadsItems(int num_items);

// Reports the overall status after attempting to migrate a user's ~/Downloads
// to ~/MyFiles/Downloads.
void ReportDownloadsMigrationStatus(DownloadsMigrationStatus status);

// Reports the success or failure of the given `operation` as UMA histogram
// `"Cryptohome.DownloadsBindMountMigration." + operation`. If `success` is
// true, this function records a success event, ie an error with the value 0. If
// `success` is false, this function records an error with the current `errno`
// value. In any case, this function preserves the current `errno` value, so
// that it can be further used.
void ReportDownloadsMigrationOperation(std::string_view operation,
                                       bool success);

// Cryptohome Error Reporting related UMAs

// Reports the full error id's hash when an error occurred.
void ReportCryptohomeErrorHashedStack(std::string error_bucket_name,
                                      const uint32_t hashed);

// Reports the leaf node and TPM error when an error occurred.
void ReportCryptohomeErrorLeafWithTPM(std::string error_bucket_name,
                                      const uint32_t mixed);

// Reports the error location when kDevCheckUnexpectedState happened.
void ReportCryptohomeErrorDevCheckUnexpectedState(std::string error_bucket_name,
                                                  const uint32_t loc);

// Reports a node in the error ID. This will be called multiple times for
// an error ID with multiple nodes.
void ReportCryptohomeErrorAllLocations(std::string error_bucket_name,
                                       const uint32_t loc);

// Call this to disable all CryptohomeError related metrics reporting. This is
// for situations in which we generate too many possible values in
// CryptohomeError related reporting.
void DisableErrorMetricsReporting();

// Reports the current state of the auth factor backing stores.
void ReportAuthFactorBackingStoreConfig(AuthFactorBackingStoreConfig config);

// Reports the result of an (attempted) migration of a keyset to USS.
void ReportVkToUssMigrationStatus(VkToUssMigrationStatus status);

// Reports the result of the backup VaultKeyset cleanup for users with
// semi-migrated users, i.e users with mixed USS-VaultKeyset configuration.
void ReportBackupKeysetCleanupResult(BackupKeysetCleanupResult status);
void ReportBackupKeysetCleanupSucessWithType(AuthFactorType auth_factor_type);
void ReportBackupKeysetCleanupFileFailureWithType(
    AuthFactorType auth_factor_type);

// Reports the emitted fingerprint enroll signal.
void ReportFingerprintEnrollSignal(
    user_data_auth::FingerprintScanResult scan_result);

// Reports the emitted fingerprint auth signal.
void ReportFingerprintAuthSignal(
    user_data_auth::FingerprintScanResult scan_result);

// Reports the BackendCertProviderUpdateCertResult; refer to the enum's comment.
void ReportBackendCertProviderUpdateCertResult(
    BackendCertProviderUpdateCertResult result);

// Reports the VerifyAndParseBackendCertResult; refer to the enum's comment.
void ReportVerifyAndParseBackendCertResult(
    VerifyAndParseBackendCertResult result);

// Initialization helper.
class ScopedMetricsInitializer {
 public:
  ScopedMetricsInitializer() { InitializeMetrics(); }
  ~ScopedMetricsInitializer() { TearDownMetrics(); }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_METRICS_H_
