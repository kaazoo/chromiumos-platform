// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stub implementation of Samba net. Does not talk to server, but simply returns
// fixed responses to predefined input.

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "authpolicy/constants.h"
#include "authpolicy/platform_helper.h"
#include "authpolicy/samba_helper.h"
#include "authpolicy/stub_common.h"

namespace authpolicy {
namespace {

const char kStubKeytab[] = "Stub keytab file";

// Various stub error messages.
const char kSmbConfArgMissingError[] =
    "Can't load /etc/samba/smb.conf - run testparm to debug it";
const char kNetworkError[] = "No logon servers";
const char kWrongPasswordError[] =
    "Failed to join domain: failed to lookup DC info for domain "
    "'REALM.EXAMPLE.COM' over rpc: Logon failure";
const char kExpiredPasswordError[] =
    "Enter user@REALM.EXAMPLE.COM's password:\n"
    "Failed to join domain: failed to lookup DC info for domain "
    "'REALM.EXAMPLE.COM' over rpc: Must change password";
const char kJoinAccessDeniedError[] =
    "Failed to join domain: Failed to set account flags for machine account "
    "(NT_STATUS_ACCESS_DENIED)";
const char kMachineNameTooLongError[] =
    "Our netbios name can be at most %zd chars long, \"%s\" is %zd chars long\n"
    "Failed to join domain: The format of the specified computer name is "
    "invalid.";
const char kInvalidMachineNameError[] =
    "Failed to join domain: failed to join domain 'REALM.EXAMPLE.COM' over "
    "rpc: Improperly formed account name";
const char kInsufficientQuotaError[] =
    "Insufficient quota exists to complete the operation";

// Size limit for machine name.
const size_t kMaxMachineNameSize = 15;

// Stub net ads info response.
const char kStubInfo[] = R"!!!(LDAP server: 111.222.33.1
LDAP server name: LDAPNAME.example.com
Realm: REALM.EXAMPLE.COM
Bind Path: dc=REALM,dc=EXAMPLE,dc=COM
LDAP port: 389
Server time: Fri, 03 Feb 2017 05:24:05 PST
KDC server: 111.222.33.2
Server time offset: -91
Last machine account password change:
Wed, 31 Dec 1969 16:00:00 PST)!!!";

// Stub net ads info response.
const char kStubLookup[] = R"!!!(Information for Domain Controller: 111.222.33.3
Response Type: LOGON_SAM_LOGON_RESPONSE_EX
GUID: fca78f31-bf15-4ca3-b730-fbe619e937b2
Flags:
    Is a PDC:                                   yes
    Is a GC of the forest:                      yes
    Is an LDAP server:                          yes
    Supports DS:                                yes
    Is running a KDC:                           yes
    Is running time services:                   yes
    Is the closest DC:                          no
    Is writable:                                yes
    Has a hardware clock:                       yes
    Is a non-domain NC serviced by LDAP server: no
    Is NT6 DC that has some secrets:            no
    Is NT6 DC that has all secrets:             yes
    Runs Active Directory Web Services:         yes
    Runs on Windows 2012 or later:              yes
Forest:             FOREST.EXAMPLE.COM
Domain:             REALM.EXAMPLE.COM
Domain Controller:  DCNAME.EXAMPLE.COM
Pre-Win2k Domain:   REALM
Pre-Win2k Hostname: DCNAME
Server Site Name :  SITE
Client Site Name :  SITE
NT Version: 5
LMNT Token: ffff
LM20 Token: ffff)!!!";

// Stub net ads gpo list response.
const char kStubLocalGpo[] = R"!!!(---------------------
name:   Local Policy
displayname:  Local Policy
version:  0 (0x00000000)
version_user:  0 (0x0000)
version_machine: 0 (0x0000)
filesyspath:  (null)
dspath:  (null)
options:  0 GPFLAGS_ALL_ENABLED
link:   (null)
link_type:  5 machine_extensions: (null)
user_extensions: (null)
)!!!";

const char kStubRemoteGpo[] = R"!!!(---------------------
name:   %s
displayname:  test-user-policy
version:  %u (0x%04x%04x)
version_user:  %u (0x%04x)
version_machine: %u (0x%04x)
filesyspath:  \\realm.example.com\SysVol\realm.example.com\Policies\%s
dspath:  cn=%s,cn=policies,cn=system,DC=realm,DC=example,DC=com
options:  %s
link:   OU=test-ou,DC=realm,DC=example,DC=com
link_type:  4 GP_LINK_OU
machine_extensions: (null)
user_extensions: [{D02B1F73-3407-48AE-BA88-E8213C6761F1}]
)!!!";

// Stub net ads search response.
const char kStubSearchFormat[] = R"!!!(Got 1 replies
objectClass: top
objectClass: person
objectClass: organizationalPerson
objectClass: user
cn: %s
sn: Doe
givenName: %s
initials: JD
distinguishedName: CN=%s,OU=test-ou,DC=realm,DC=example,DC=com
instanceType: 4
whenCreated: 20161018155136.0Z
whenChanged: 20170217134227.0Z
displayName: %s
uSNCreated: 287406
uSNChanged: 307152
name: John Doe
objectGUID: %s
userAccountControl: %u
badPwdCount: 0
codePage: 0
countryCode: 0
badPasswordTime: 131309487458845506
lastLogoff: 0
lastLogon: 131320568639495686
pwdLastSet: %lu
primaryGroupID: 513
objectSid: S-1-5-21-250062649-3667841115-373469193-1134
accountExpires: 9223372036854775807
logonCount: 1453
sAMAccountName: %s
sAMAccountType: 805306368
userPrincipalName: jdoe@realm.example.com
objectCategory: CN=Person,CN=Schema,CN=Configuration,DC=realm,DC=example,DC=com
dSCorePropagationData: 20161024075536.0Z
dSCorePropagationData: 20161024075311.0Z
dSCorePropagationData: 20161019075502.0Z
dSCorePropagationData: 16010101000000.0Z
lastLogonTimestamp: 131318125471489990
msDS-SupportedEncryptionTypes: 0)!!!";

// Search that doesn't find anything.
const char kStubBadSearch[] = "Got 0 replies";

// Builder for custom search results (without having a 7-line base::StringPrintf
// every time). Usage:
//   search_result = SearchBuilder().SetDisplayName("John Doe").GetResult();
class SearchBuilder {
 public:
  // Prints out a stub net ads search result with the set parameters.
  std::string GetResult() {
    return base::StringPrintf(
        kStubSearchFormat, common_name_.c_str(), given_name_.c_str(),
        common_name_.c_str(), display_name_.c_str(), object_guid_.c_str(),
        user_account_control_, pwd_last_set_, sam_account_name_.c_str());
  }

  // Sets the value of the givenName key.
  SearchBuilder& SetGivenName(const std::string& value) {
    given_name_ = value;
    return *this;
  }

  // Sets the value of the displayName key.
  SearchBuilder& SetDisplayName(const std::string& value) {
    display_name_ = value;
    return *this;
  }

  // Sets the value of the objectUID key.
  SearchBuilder& SetObjectGuid(const std::string& value) {
    object_guid_ = value;
    return *this;
  }

  // Sets the value of the sAMAccountName key.
  SearchBuilder& SetSAMAccountName(const std::string& value) {
    sam_account_name_ = value;
    return *this;
  }

  // Sets the value of the common name key.
  SearchBuilder& SetCommonName(const std::string& value) {
    common_name_ = value;
    return *this;
  }

  // Sets the value of the userAccountControl key.
  SearchBuilder& SetUserAccountControl(const uint32_t value) {
    user_account_control_ = value;
    return *this;
  }

  // Sets the value of the pwdLastSet key.
  SearchBuilder& SetPwdLastSet(const uint64_t value) {
    pwd_last_set_ = value;
    return *this;
  }

 private:
  std::string given_name_ = kGivenName;
  std::string display_name_ = kDisplayName;
  std::string object_guid_ = kAccountId;
  std::string sam_account_name_ = kUserName;
  std::string common_name_ = kCommonName;
  uint32_t user_account_control_ = kUserAccountControl;
  uint64_t pwd_last_set_ = kPwdLastSet;
};

// Searches |str| for (|searchKey|=value) and returns value. Returns an empty
// string if the key could not be found or if the value is empty.
std::string FindSearchValue(const std::string& str, const char* search_key) {
  const std::string full_key = base::StringPrintf("(%s=", search_key);
  size_t idx1 = str.find(full_key);
  if (idx1 == std::string::npos)
    return "";
  const size_t idx2 = str.find(")", idx1 + full_key.size());
  if (idx2 == std::string::npos)
    return "";
  idx1 += full_key.size();
  return str.substr(idx1, idx2 - idx1);
}

// Prints custom stub net ads gpo list output corresponding to one remote GPO
// with the given properties. For |gpflags| see kGpFlag*.
std::string PrintGpo(const char* guid,
                     uint32_t version_user,
                     uint32_t version_machine,
                     int gpflags) {
  DCHECK(gpflags >= 0 && gpflags < kGpFlagCount);
  return base::StringPrintf(
      kStubRemoteGpo, guid, (version_user << 16) | version_machine,
      version_user, version_machine, version_user, version_user,
      version_machine, version_machine, guid, guid, kGpFlagsStr[gpflags]);
}

// Writes a fake keytab file.
void WriteKeytabFile() {
  std::string keytab_path = GetKeytabFilePath();
  CHECK(!keytab_path.empty());
  // Note: base::WriteFile triggers a seccomp failure, so do it old-school.
  base::ScopedFILE kt_file(fopen(keytab_path.c_str(), "w"));
  CHECK(kt_file);
  CHECK_EQ(1U, fwrite(kStubKeytab, strlen(kStubKeytab), 1, kt_file.get()));
}

// Reads the smb.conf file at |smb_conf_path| and extracts the netbios name.
std::string GetMachineNameFromSmbConf(const std::string& smb_conf_path) {
  // We need the device smb.conf here, the user smb.conf doesn't contain the
  // netbios name.
  std::string device_smb_conf_path = smb_conf_path;
  base::ReplaceFirstSubstringAfterOffset(&device_smb_conf_path, 0,
                                         "smb_user.conf", "smb_device.conf");
  std::string smb_conf;
  CHECK(
      base::ReadFileToString(base::FilePath(device_smb_conf_path), &smb_conf));
  std::string machine_name;
  CHECK(FindToken(smb_conf, '=', "netbios name", &machine_name));
  return machine_name;
}

// Returns different stub net ads search results depending on |object_guid|.
std::string GetSearchResultFromObjectGUID(const std::string& object_guid) {
  SearchBuilder search_builder;
  search_builder.SetObjectGuid(object_guid);

  // Valid account id, return valid search result for the default user.
  if (object_guid == kAccountId)
    return search_builder.GetResult();

  // Invalid account id, return bad "nothing found" search result.
  if (object_guid == kBadAccountId)
    return kStubBadSearch;

  // Pretend that the password expired.
  if (object_guid == kExpiredPasswordAccountId)
    return search_builder.SetPwdLastSet(0).GetResult();

  // Pretend that the password never expires.
  if (object_guid == kNeverExpirePasswordAccountId) {
    return search_builder.SetPwdLastSet(0)
        .SetUserAccountControl(UF_DONT_EXPIRE_PASSWD)
        .GetResult();
  }

  // Pretend that the password changed on the server.
  if (object_guid == kPasswordChangedAccountId)
    return search_builder.SetPwdLastSet(kPwdLastSet + 1).GetResult();

  NOTREACHED() << "UNHANDLED OBJECT GUID " << object_guid;
  return std::string();
}

// Returns different stub net ads search results depending on
// |sam_account_name|.
std::string GetSearchResultFromSAMAccountName(
    const std::string& sam_account_name) {
  SearchBuilder search_builder;
  search_builder.SetSAMAccountName(sam_account_name);

  // Set a special |kPasswordChangedAccountId|, required during auth for a
  // test that uses that id in GetUserStatus().
  if (sam_account_name == kPasswordChangedUserName)
    return search_builder.SetObjectGuid(kPasswordChangedAccountId).GetResult();

  // In all cases, just return a search result with the proper sAMAccountName.
  return search_builder.GetResult();
}

// Handles a stub 'net ads workgroup' call. Just returns a fake workgroup.
int HandleWorkgroup() {
  WriteOutput("Workgroup: WOKGROUP", "");
  return kExitCodeOk;
}

// Handles a stub 'net ads join' call. Different behavior is triggered by
// passing different user principals, passwords and machine names (in smb.conf).
int HandleJoin(const std::string& command_line,
               const std::string& smb_conf_path) {
  // Read the password from stdin.
  std::string password;
  if (!ReadPipeToString(STDIN_FILENO, &password)) {
    LOG(ERROR) << "Failed to read password";
    return kExitCodeError;
  }
  const std::string kUserFlag = "-U ";
  const std::string kCreatecomputer = "createcomputer=";

  // Read machine name from smb.conf.
  const std::string machine_name = GetMachineNameFromSmbConf(smb_conf_path);
  CHECK(!machine_name.empty());

  // Stub too long machine name error.
  if (machine_name.size() > kMaxMachineNameSize) {
    WriteOutput(
        base::StringPrintf(kMachineNameTooLongError, kMaxMachineNameSize,
                           machine_name.c_str(), machine_name.size()),
        "");
    return kExitCodeError;
  }

  // Stub bad machine name error.
  if (machine_name == base::ToUpperASCII(kInvalidMachineName)) {
    WriteOutput(kInvalidMachineNameError, "");
    return kExitCodeError;
  }

  // Stub insufficient quota error.
  if (Contains(command_line, kUserFlag + kInsufficientQuotaUserPrincipal)) {
    WriteOutput(kInsufficientQuotaError, "");
    return kExitCodeError;
  }

  // Stub non-existing account error (same error as 'wrong password' error).
  if (Contains(command_line, kUserFlag + kNonExistingUserPrincipal)) {
    WriteOutput(kWrongPasswordError, "");
    return kExitCodeError;
  }

  // Stub network error.
  if (Contains(command_line, kUserFlag + kNetworkErrorUserPrincipal)) {
    WriteOutput("", kNetworkError);
    return kExitCodeError;
  }

  // Stub access denied error.
  if (Contains(command_line, kUserFlag + kAccessDeniedUserPrincipal)) {
    WriteOutput(kJoinAccessDeniedError, "");
    return kExitCodeError;
  }

  // Check whether createcomputer argument matches the expected one.
  if (Contains(command_line, kUserFlag + kExpectOuUserPrincipal)) {
    CHECK(Contains(command_line, kCreatecomputer + kExpectedOuCreatecomputer))
        << "Bad createcomputer arg in command line " << command_line
        << ". Expected: " << kExpectedOuCreatecomputer;
    WriteKeytabFile();
    return kExitCodeOk;
  }

  // Stub valid user principal. Switch behavior based on password.
  if (Contains(command_line, kUserFlag + kUserPrincipal)) {
    // Stub wrong password.
    if (password == kWrongPassword) {
      WriteOutput(kWrongPasswordError, "");
      return kExitCodeError;
    }
    // Stub expired password.
    if (password == kExpiredPassword) {
      WriteOutput(kExpiredPasswordError, "");
      return kExitCodeError;
    }
    // Stub valid password.
    if (password == kPassword) {
      WriteKeytabFile();
      return kExitCodeOk;
    }
    NOTREACHED() << "UNHANDLED PASSWORD " << password;
    return kExitCodeError;
  }

  NOTREACHED() << "UNHANDLED COMMAND LINE " << command_line;
  return kExitCodeError;
}

// Handles a stub 'net ads info' call. Just returns stub information.
int HandleInfo() {
  WriteOutput(kStubInfo, "");
  return kExitCodeOk;
}

// Handles a stub 'net ads lookup' call. Just returns stub information.
int HandleLookup() {
  WriteOutput(kStubLookup, "");
  return kExitCodeOk;
}

// Handles a stub 'net ads gpo list' call. Different behavior is triggered by
// passing different machine names (in smb.conf).
int HandleGpoList(const std::string& smb_conf_path) {
  // Read machine name from smb.conf.
  const std::string machine_name = GetMachineNameFromSmbConf(smb_conf_path);
  CHECK(!machine_name.empty());

  // Stub empty GPO list.
  if (machine_name == base::ToUpperASCII(kEmptyGpoMachineName))
    return kExitCodeOk;

  // All other GPO lists use the local GPO.
  std::string gpos = kStubLocalGpo;

  if (machine_name == base::ToUpperASCII(kGpoDownloadErrorMachineName)) {
    // Stub GPO list that triggers a download error in smbclient.
    gpos += PrintGpo(kErrorGpoGuid, 1, 1, kGpFlagAllEnabled);
  } else if (machine_name == base::ToUpperASCII(kOneGpoMachineName)) {
    // Stub GPO list that downloads one GPO if present.
    gpos += PrintGpo(kGpo1Guid, 1, 1, kGpFlagAllEnabled);
  } else if (machine_name == base::ToUpperASCII(kTwoGposMachineName)) {
    // Stub GPO list that downloads two GPOs if present.
    gpos += PrintGpo(kGpo1Guid, 1, 1, kGpFlagAllEnabled);
    gpos += PrintGpo(kGpo2Guid, 1, 1, kGpFlagAllEnabled);
  } else if (machine_name == base::ToUpperASCII(kZeroUserVersionMachineName)) {
    // Stub GPO list that contains a GPO with version_user == 0 (should be
    // ignored during user policy fetch).
    gpos += PrintGpo(kGpo1Guid, 0, 1, kGpFlagAllEnabled);
  } else if (machine_name == base::ToUpperASCII(kDisableUserFlagMachineName)) {
    // Stub GPO list that contains a GPO with kGpFlagUserDisabled set (should be
    // ignored during user policy fetch).
    gpos += PrintGpo(kGpo1Guid, 1, 1, kGpFlagUserDisabled);
  }

  WriteOutput("", gpos);
  return kExitCodeOk;
}

// Handles a stub 'net ads search' call. Different behavior is triggered by
// passing different sAMAccountNames or objectGUIDs as search term.
int HandleSearch(const std::string& command_line) {
  std::string sam_account_name =
      FindSearchValue(command_line, kSearchSAMAccountName);
  std::string object_guid_octet =
      FindSearchValue(command_line, kSearchObjectGUID);
  std::string search_result;
  if (!object_guid_octet.empty()) {
    // Search by objectGUID aka account id.
    std::string object_guid = OctetStringToGuidForTesting(object_guid_octet);
    search_result = GetSearchResultFromObjectGUID(object_guid);
  } else if (!sam_account_name.empty()) {
    // Search by sAMAccountName.
    search_result = GetSearchResultFromSAMAccountName(sam_account_name);
  } else {
    LOG(ERROR) << "SEARCH TERM NOT RECOGNIZED IN COMMAND LINE " << command_line;
  }

  WriteOutput(search_result, "");
  return kExitCodeOk;
}

int HandleCommandLine(const std::string& command_line,
                      const std::string& smb_conf_path) {
  // Make sure the caller adds the debug level.
  CHECK(Contains(command_line, " -d "));

  // Stub net ads workgroup.
  if (StartsWithCaseSensitive(command_line, "ads workgroup"))
    return HandleWorkgroup();

  // Stub net ads join.
  if (StartsWithCaseSensitive(command_line, "ads join"))
    return HandleJoin(command_line, smb_conf_path);

  // Stub net ads info.
  if (StartsWithCaseSensitive(command_line, "ads info"))
    return HandleInfo();

  // Stub net ads lookup.
  if (StartsWithCaseSensitive(command_line, "ads lookup"))
    return HandleLookup();

  // Stub net ads gpo list.
  if (StartsWithCaseSensitive(command_line, "ads gpo list"))
    return HandleGpoList(smb_conf_path);

  // Stub net ads search.
  if (StartsWithCaseSensitive(command_line, "ads search"))
    return HandleSearch(command_line);

  NOTREACHED() << "UNHANDLED COMMAND LINE " << command_line;
  return kExitCodeError;
}

}  // namespace
}  // namespace authpolicy

int main(int argc, char* argv[]) {
  // Find Samba configuration path ("-s" argument).
  const std::string smb_conf_path = authpolicy::GetArgValue(argc, argv, "-s");
  if (smb_conf_path.empty()) {
    authpolicy::WriteOutput("", authpolicy::kSmbConfArgMissingError);
    return authpolicy::kExitCodeError;
  }

  const std::string command_line = authpolicy::GetCommandLine(argc, argv);
  return authpolicy::HandleCommandLine(command_line, smb_conf_path);
}
