#ifndef APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
#define APPTRAVERSE_SHARED_SYNC_RUNTIME_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/byte_transport.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse {

// One replica's shared synchronization runtime: it routes opaque transport
// bytes to the SharedNode named by the frame and owns the durable ordering of
// the initial-state exchange.
//
// Protocol v1 carries NodeState, Ack, and standalone Event. Dynamic object
// graphs, heartbeat, and presence are later milestones.
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

  // Bootstrap permission from an authorized source endpoint without knowing
  // the node ObjId in advance. Requires root's most-derived class to match.
  // When expected_node_id is set, frame.target_node_id must match before import.
  // The two-argument call stays compatible (empty expected_node_id).
  void ExpectInitialNodeFromEndpoint(std::string source_endpoint,
                                     std::uint32_t expected_root_class_id,
                                     ae::ObjId expected_node_id = {});

  // Remove a pending endpoint expectation only. Does not unregister nodes or
  // delete stored data.
  void ForgetInitialNodeFromEndpoint(std::string const& source_endpoint);

  using InitialNodeImportedCallback =
      std::function<bool(std::string const& source_endpoint,
                         SharedNode::ptr node)>;

  void SetInitialNodeImportedCallback(InitialNodeImportedCallback callback);

  SharedNode::ptr FindNode(ae::ObjId node_id) const;

  // Drive initial state for one Share relationship:
  //   NotStarted - freeze the packet, persist it, then send it
  //   Pending    - resend the persisted packet byte for byte
  //   Complete   - acknowledged, nothing to send
  void SyncInitialState(ae::ObjId node_id, ae::ObjId share_id);

  // Drive one incremental standalone Event for a Complete relationship:
  //   pending packet exists - resend those exact bytes
  //   otherwise freeze the first undelivered shared journal Event, persist,
  //   then send
  //   otherwise nothing
  void SyncNextEvent(ae::ObjId node_id, ae::ObjId share_id);

  // Allowlist scalar Event classes permitted for standalone network sync.
  void AllowStandaloneEventClass(std::uint32_t class_id);

 private:
  bool IsStandaloneEventClassAllowed(std::uint32_t class_id) const;

  static void ReceiveThunk(void* ctx, std::string const& source_endpoint,
                           std::vector<std::uint8_t> const& bytes);

  void OnBytes(std::string const& source_endpoint,
               std::vector<std::uint8_t> const& bytes);
  void OnNodeState(std::string const& source_endpoint,
                   NodeStateFrame const& frame);
  void OnAck(std::string const& source_endpoint, AckFrame const& frame);
  void OnEvent(std::string const& source_endpoint, EventFrame const& frame);

  struct ImportedNode {
    SharedNode::ptr node;
    ae::ObjId source_share_id;
  };

  // Admit an expected but unknown root: parse and validate the snapshot in a
  // scratch Domain, and only then write it into this replica's storage.
  // Returns an invalid ptr when the frame is rejected, having written nothing.
  ImportedNode ImportValidatedNode(std::string const& source_endpoint,
                                   NodeStateFrame const& frame);

  bool IsExpectedInitialNode(ae::ObjId node_id) const;

  struct EndpointExpectation {
    std::string source_endpoint;
    std::uint32_t expected_root_class_id{0};
    ae::ObjId expected_node_id;
  };

  ae::Domain& domain_;
  ae::IDomainStorage& storage_;
  IByteTransport& transport_;
  std::vector<SharedNode::ptr> nodes_;
  std::vector<ae::ObjId> expected_initial_nodes_;
  std::vector<EndpointExpectation> expected_endpoint_nodes_;
  InitialNodeImportedCallback initial_node_imported_callback_;
  std::vector<std::uint32_t> standalone_event_classes_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
