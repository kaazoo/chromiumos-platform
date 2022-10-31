// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_PLUGINS_H_
#define SECAGENTD_PLUGINS_H_

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "base/memory/scoped_refptr.h"
#include "missive/proto/security_xdr_events.pb.h"
#include "secagentd/bpf_skeleton_wrappers.h"
#include "secagentd/message_sender.h"
#include "secagentd/process_cache.h"

namespace secagentd {

class PluginInterface {
 public:
  // Activate the plugin.
  virtual absl::Status Activate() = 0;
  virtual std::string GetName() const = 0;
  virtual ~PluginInterface() = default;
};

class ProcessPlugin : public PluginInterface {
 public:
  ProcessPlugin(scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory,
                scoped_refptr<MessageSenderInterface> message_sender,
                scoped_refptr<ProcessCacheInterface> process_cache);
  // Load, verify and attach the process BPF applications.
  absl::Status Activate() override;
  std::string GetName() const override;

  void HandleRingBufferEvent(const bpf::cros_event& bpf_event);
  void HandleBpfRingBufferReadReady() const;

 private:
  std::unique_ptr<cros_xdr::reporting::XdrProcessEvent> MakeExecEvent(
      const secagentd::bpf::cros_process_start& process_start);
  std::unique_ptr<cros_xdr::reporting::XdrProcessEvent> MakeTerminateEvent(
      const secagentd::bpf::cros_process_exit& process_exit);
  // This is static because it must be accessible to a C style function.
  static struct BpfCallbacks callbacks_;
  base::WeakPtrFactory<ProcessPlugin> weak_ptr_factory_;
  scoped_refptr<MessageSenderInterface> message_sender_;
  scoped_refptr<ProcessCacheInterface> process_cache_;
  scoped_refptr<BpfSkeletonFactoryInterface> factory_;
  std::unique_ptr<BpfSkeletonInterface> skeleton_wrapper_;
};

class PluginFactoryInterface {
 public:
  enum class PluginType { kAgent, kProcess };

  virtual std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache) = 0;
  virtual ~PluginFactoryInterface() = default;
};

namespace Types {
using Plugin = PluginFactoryInterface::PluginType;
}  // namespace Types

// Support absl format for PluginType.
absl::FormatConvertResult<absl::FormatConversionCharSet::kString>
AbslFormatConvert(const PluginFactoryInterface::PluginType& type,
                  const absl::FormatConversionSpec& conversion_spec,
                  absl::FormatSink* output_sink);

// Support streaming for PluginType.
std::ostream& operator<<(std::ostream& out,
                         const PluginFactoryInterface::PluginType& type);

class PluginFactory : public PluginFactoryInterface {
 public:
  PluginFactory();
  explicit PluginFactory(
      scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory)
      : bpf_skeleton_factory_(bpf_skeleton_factory) {}
  std::unique_ptr<PluginInterface> Create(
      PluginType type,
      scoped_refptr<MessageSenderInterface> message_sender,
      scoped_refptr<ProcessCacheInterface> process_cache) override;

 private:
  scoped_refptr<BpfSkeletonFactoryInterface> bpf_skeleton_factory_;
};

}  // namespace secagentd
#endif  // SECAGENTD_PLUGINS_H_
