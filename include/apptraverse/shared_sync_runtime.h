#ifndef APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
#define APPTRAVERSE_SHARED_SYNC_RUNTIME_H_

#include <cstdint>
#include <string>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/memory_transport.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse {

// One replica's shared synchronization runtime: it routes opaque transport
// bytes to the SharedNode named by the frame and owns the durable ordering of
// the initial-state exchange.
//
// Protocol v1 carries NodeState and Ack only. Incremental Event replication,
// heartbeat, and presence are later milestones.
//
// Instance-scoped: the runtime holds its replica's Domain, storage, and
// transport. Nothing is process-global or thread_local, and no model pointer
// ever crosses a replica boundary — only bytes do.
class SharedSyncRuntime {
 public:
  SharedSyncRuntime(ae::Domain& domain, ae::IDomainStorage& storage,
                    IByteTransport& transport);
  ~SharedSyncRuntime();

  SharedSyncRuntime(SharedSyncRuntime const&) = delete;
  SharedSyncRuntime& operator=(SharedSyncRuntime const&) = delete;

  // SharedNodes this replica already participates in (created locally or
  // loaded from its own storage after a restart).
  void RegisterNode(SharedNode::ptr node);

  // Bootstrap permission for a SharedNode this replica does not have yet.
  // Without it, incoming bytes cannot create a new root.
  void ExpectInitialNode(ae::ObjId node_id);

  SharedNode::ptr FindNode(ae::ObjId node_id) const;

  // Drive initial state for one Share relationship:
  //   NotStarted - freeze the packet, persist it, then send it
  //   Pending    - resend the persisted packet byte for byte
  //   Complete   - acknowledged, nothing to send
  void SyncInitialState(ae::ObjId node_id, ae::ObjId share_id);

 private:
  static void ReceiveThunk(void* ctx, std::string const& source_endpoint,
                           std::vector<std::uint8_t> const& bytes);

  void OnBytes(std::string const& source_endpoint,
               std::vector<std::uint8_t> const& bytes);
  void OnNodeState(std::string const& source_endpoint,
                   NodeStateFrame const& frame);
  void OnAck(AckFrame const& frame);

  bool IsExpectedInitialNode(ae::ObjId node_id) const;

  ae::Domain& domain_;
  ae::IDomainStorage& storage_;
  IByteTransport& transport_;
  std::vector<SharedNode::ptr> nodes_;
  std::vector<ae::ObjId> expected_initial_nodes_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
