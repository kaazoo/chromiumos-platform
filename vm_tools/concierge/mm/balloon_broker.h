// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_

#include <memory>
#include <string>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/sequence_checker.h>
#include <base/task/sequenced_task_runner.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/balloon_blocker.h"
#include "vm_tools/concierge/mm/kills_server.h"

using vm_tools::vm_memory_management::ResizePriority;

namespace vm_tools::concierge::mm {

// The BalloonBroker is the main entrypoint into adjusting the size of
// virtio-balloons managed by the VM Memory Management Service. The
// BalloonBroker must be kept in sync with current VM lifecycle through the
// RegisterVm() and RemoveVm() functions. Callers can query the block
// state of a specific VM's balloon through the LowestUnblockedPriority()
// function and can also request to reclaim memory from a specific context
// (including the host) by using the Reclaim() function. Additionally, the
// BalloonBroker registers itself as the handler of kill decision requests and
// no kill candidate notifications that are received by the
// KillsServer.
class BalloonBroker {
 public:
  // Creates balloon instances.
  using BalloonBlockerFactory = std::function<std::unique_ptr<BalloonBlocker>(
      int, const std::string&, scoped_refptr<base::SequencedTaskRunner>)>;

  explicit BalloonBroker(
      std::unique_ptr<KillsServer> kills_server,
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner,
      BalloonBlockerFactory balloon_blocker_factory =
          &BalloonBroker::CreateBalloonBlocker);

  BalloonBroker(const BalloonBroker&) = delete;
  BalloonBroker& operator=(const BalloonBroker&) = delete;

  // Registers a VM and the corresponding control socket with the broker.
  void RegisterVm(int vm_cid, const std::string& socket_path);

  // Removes a VM and its corresponding balloon from the broker.
  void RemoveVm(int vm_cid);

  // Returns the lowest ResizePriority among all balloons that will not be
  // blocked. If all balloons are blocked at the highest priority,
  // RESIZE_PRIORITY_UNSPECIFIED is returned.
  ResizePriority LowestUnblockedPriority() const;

  // A reclaim operation consists of reclaim from one or more contexts. This can
  // be represented as a set mapping a CID to a number of bytes to reclaim.
  using ReclaimOperation = base::flat_map<int, size_t>;
  // Performs the specified reclaim operations at |priority|.
  void Reclaim(const ReclaimOperation& reclaim_targets,
               ResizePriority priority);

  // Reclaim from |vm_cid| until the request is blocked at |priority|.
  void ReclaimUntilBlocked(int vm_cid, ResizePriority priority);

 private:
  // Contains state related to a client that is connected to the VM memory
  // management service (i.e. resourced, ARCVM's LMKD).
  struct BalloonBrokerClient {
    // The corresponding client from the server.
    Client mm_client;

    // Whether this client currently has kill candidates.
    bool has_kill_candidates = true;

    // The priority of the most recent kill request from this client.
    ResizePriority kill_request_priority =
        ResizePriority::RESIZE_PRIORITY_UNSPECIFIED;

    // The result of the most recent kill request from this client.
    int64_t kill_request_result = 0;
  };

  // Contains state related to a specific context (i.e. host, ARCVM).
  struct Context {
    // The balloon blocker instance for this context (remains null for the
    // host's context).
    std::unique_ptr<BalloonBlocker> balloon;

    // All of the clients that have connected from this context.
    // TODO(b:307477987) Originally both Ash and Lacros were separate clients on
    // the host and thus the BalloonBroker needed to support multiple clients
    // from one context. Since this is no longer the case, this logic can be
    // simplified to only have one client from each context.
    std::vector<BalloonBrokerClient> clients;
  };

  // The amount to adjust the balloon if there are no kill candidates in a
  // context, but it is facing persistent memory pressure.
  //
  // This is purposefully large so that in the case of high host memory pressure
  // with low guest memory pressure the balloon inflates quickly. If the
  // balloon is under contention then this amount will be capped by the
  // balloon's kBalloonContentionMaxOperationSizeBytes.
  static constexpr int64_t kNoKillCandidatesReclaimAmount = MiB(128);

  // Creates a balloon.
  static std::unique_ptr<BalloonBlocker> CreateBalloonBlocker(
      int vm_cid,
      const std::string& socket_path,
      scoped_refptr<base::SequencedTaskRunner> balloon_operations_task_runner);

  // START: Server Callbacks.

  // Callback to be run when a new client is connected to the VM memory
  // management service.
  void OnNewClientConnected(Client client);

  // Callback to be run when a client disconnects from the VM memory management
  // service.
  void OnClientDisconnected(Client client);

  // Callback to be run when a client requests a kill decision.
  size_t HandleKillRequest(Client client,
                           size_t proc_size,
                           ResizePriority priority);

  // Callback to be run when a client has no kill candidates.
  void HandleNoKillCandidates(Client client);

  // Callback to be run when a decision latency packet is received.
  void HandleDecisionLatency(Client client, const DecisionLatency& latency);

  // END: Server Callbacks.

  // Attempts to evenly adjust the target balloons at the target priority.
  // Returns the actual total adjustment.
  int64_t EvenlyAdjustBalloons(const base::flat_set<int>& targets,
                               int64_t total_adjustment,
                               ResizePriority priority);

  // Adjusts the balloon for |cid| by |adjustment| at |priority|. Returns the
  // actual balloon delta in bytes.
  int64_t AdjustBalloon(int cid, int64_t adjustment, ResizePriority priority);

  // Returns the BalloonBrokerClient that corresponds to |client|.
  BalloonBrokerClient* GetBalloonBrokerClient(Client client);

  // Sets the kill candidate state for the specified client.
  void SetHasKillCandidates(Client client, bool has_candidates);

  // Sets the kill request result for the client.
  void SetMostRecentKillRequest(Client client,
                                ResizePriority priority,
                                int64_t result);

  // The amount to reclaim for every iteration of ReclaimUntilBlocked().
  static constexpr int64_t kReclaimIncrement = MiB(128);

  // The server that listens for and handles kills related messages.
  const std::unique_ptr<KillsServer> kills_server_;

  // The task runner on which to run balloon operations.
  const scoped_refptr<base::SequencedTaskRunner>
      balloon_operations_task_runner_;

  // Creates balloon instances.
  const BalloonBlockerFactory balloon_blocker_factory_;

  // Ensure calls are made on the right sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // Maps from a CID (int) to a Context state.
  base::flat_map<int, Context> contexts_
      GUARDED_BY_CONTEXT(sequence_checker_){};

  // Maintains the list of VMs that are currently connected.
  base::flat_set<int> connected_vms_ GUARDED_BY_CONTEXT(sequence_checker_){};
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_BROKER_H_
