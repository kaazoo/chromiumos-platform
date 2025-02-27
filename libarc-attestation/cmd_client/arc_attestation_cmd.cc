// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <base/base64.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>
#include <libarc_attestation/proto_bindings/arc_attestation_blob.pb.h>

#include "libarc-attestation/arc_attestation_cmd.pb.h"
#include "libarc-attestation/common/print_arc_attestation_cmd_proto.h"
#include "libarc-attestation/lib/interface.h"

namespace {

constexpr char kNonBlockingTimeoutSwitch[] = "non_blocking_timeout";

constexpr char kUsage[] = R"(
Usage: arc-attestation-cmd <command> [<options/arguments>]

Commands:
  provision
      Attempt to provision the ARC device key.
      This comand is blocking.
  get_cert_chain
      Print the ARC device key certificate chain.
  sign
      Sign the input data with ARC device key.
      The input data is specified through --data=<base64 data>
  quote_cros_blob
      Produce a ChromeOS-specific quotation blob with the given challenge.
      The challenge is specified through --data=<base64 data>
  get_endorsement_public_key
      Fetch the Endorsement Public Key of the device.

Options:
  --binary
      Output protobuf in serialized binary format (machine readable form).

  --non_blocking_timeout=<time in seconds>
      If specified, the provision process will be non-blocking, but this
      process will wait for the specified time before checking the result.
)";

constexpr char kCommandProvision[] = "provision";
constexpr char kCommandGetCertChain[] = "get_cert_chain";
constexpr char kCommandSign[] = "sign";
constexpr char kCommandQuoteCrOSBlob[] = "quote_cros_blob";
constexpr char kCommandGetEndorsementPublicKey[] = "get_endorsement_public_key";

constexpr char kDataSwitch[] = "data";

void PrintUsage() {
  fprintf(stderr, "%s", kUsage);
}

template <typename T>
void PrintResultProtobuf(bool binary, const T& msg) {
  if (binary) {
    std::string output = msg.SerializeAsString();
    fwrite(output.data(), 1, output.size(), stdout);
  } else {
    printf("%s\n", GetProtoDebugString(msg).c_str());
  }
}

arc_attestation::PrintableAndroidStatus AndroidStatusToProtobuf(
    const arc_attestation::AndroidStatus& status) {
  arc_attestation::PrintableAndroidStatus result;
  result.set_exception(status.get_exception());
  result.set_error_code(status.get_error_code());
  result.set_msg(status.get_message());
  return result;
}

bool GetBase64DataFromCmd(base::CommandLine* command_line,
                          const std::string& swi,
                          brillo::Blob& data) {
  std::string b64 = command_line->GetSwitchValueASCII(swi);
  if (!b64.size()) {
    LOG(ERROR) << "Switch " << swi << " is not available.";
    return false;
  }

  std::string result;
  if (!base::Base64Decode(b64, &result, base::Base64DecodePolicy::kForgiving)) {
    LOG(ERROR) << "Value specified by switch " << swi
               << " is not a valid base64 encoding.";
    return false;
  }

  data = brillo::Blob(result.begin(), result.end());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();

  if (command_line->HasSwitch("help") || command_line->HasSwitch("h") ||
      command_line->GetArgs().size() == 0) {
    PrintUsage();
    return 0;
  }

  std::string command = command_line->GetArgs()[0];
  if (command == kCommandProvision) {
    bool non_blocking = command_line->HasSwitch(kNonBlockingTimeoutSwitch);
    int non_blocking_timeout = 0;
    if (non_blocking) {
      std::string non_blocking_switch =
          command_line->GetSwitchValueASCII(kNonBlockingTimeoutSwitch);
      non_blocking_timeout = std::stoi(non_blocking_switch);
    }

    arc_attestation::AndroidStatus status =
        arc_attestation::ProvisionDkCert(!non_blocking);

    // Convert to protobuf.
    arc_attestation::ProvisionCmdResult result;
    *result.mutable_status() = AndroidStatusToProtobuf(status);

    // Output the result.
    PrintResultProtobuf(command_line->HasSwitch("binary"), result);
    if (!non_blocking) {
      return (status.is_ok()) ? 0 : 1;
    }

    // This will be reached only when |non_blocking = true|.
    // For non-blocking operation, we'll need to wait and try again.
    if (non_blocking_timeout < 0) {
      LOG(ERROR) << "Illegal value entered for timeout";
      return 1;
    }
    base::PlatformThread::Sleep(base::Seconds(non_blocking_timeout));

    // Retry after timeout.
    status = arc_attestation::ProvisionDkCert(!non_blocking);

    // Convert to protobuf.
    *result.mutable_status() = AndroidStatusToProtobuf(status);

    // Output the result.
    PrintResultProtobuf(command_line->HasSwitch("binary"), result);

    return (status.is_ok()) ? 0 : 1;
  } else if (command == kCommandGetCertChain) {
    arc_attestation::AndroidStatus status =
        arc_attestation::ProvisionDkCert(true);
    CHECK(status.is_ok());

    std::vector<brillo::Blob> certs;
    status = arc_attestation::GetDkCertChain(certs);

    // Convert to protouf.
    arc_attestation::GetCertChainCmdResult result;
    *result.mutable_status() = AndroidStatusToProtobuf(status);
    for (const auto& c : certs) {
      result.add_certs(std::string(c.begin(), c.end()));
    }

    // Output the result.
    PrintResultProtobuf(command_line->HasSwitch("binary"), result);
    return (status.is_ok()) ? 0 : 1;
  } else if (command == kCommandSign) {
    arc_attestation::AndroidStatus status =
        arc_attestation::ProvisionDkCert(true);
    CHECK(status.is_ok());

    brillo::Blob data;
    CHECK(GetBase64DataFromCmd(command_line, kDataSwitch, data));
    brillo::Blob signature;
    status = arc_attestation::SignWithP256Dk(data, signature);

    // Convert to protobuf.
    arc_attestation::SignCmdResult result;
    *result.mutable_status() = AndroidStatusToProtobuf(status);
    result.set_signature(std::string(signature.begin(), signature.end()));

    // Output the result.
    PrintResultProtobuf(command_line->HasSwitch("binary"), result);
    return (status.is_ok()) ? 0 : 1;
  } else if (command == kCommandQuoteCrOSBlob) {
    arc_attestation::AndroidStatus status =
        arc_attestation::ProvisionDkCert(true);
    CHECK(status.is_ok());

    brillo::Blob challenge;
    CHECK(GetBase64DataFromCmd(command_line, kDataSwitch, challenge));
    brillo::Blob blob;
    status = arc_attestation::QuoteCrOSBlob(challenge, blob);

    // Convert to protobuf.
    arc_attestation::QuoteCrOSBlobCmdResult result;
    *result.mutable_status() = AndroidStatusToProtobuf(status);
    result.mutable_blob()->ParseFromString(brillo::BlobToString(blob));

    // Output the result.
    PrintResultProtobuf(command_line->HasSwitch("binary"), result);
    return (status.is_ok()) ? 0 : 1;
  } else if (command == kCommandGetEndorsementPublicKey) {
    arc_attestation::AndroidStatus status =
        arc_attestation::ProvisionDkCert(true);
    CHECK(status.is_ok());

    brillo::Blob ek_public_key;
    status = arc_attestation::GetEndorsementPublicKey(ek_public_key);

    // Convert to protobuf.
    arc_attestation::GetEndorsementPublicKeyCmdResult result;
    *result.mutable_status() = AndroidStatusToProtobuf(status);
    result.set_blob(brillo::BlobToString(ek_public_key));

    // Output the result.
    PrintResultProtobuf(command_line->HasSwitch("binary"), result);
    return (status.is_ok()) ? 0 : 1;
  } else {
    PrintUsage();
  }

  return 0;
}
