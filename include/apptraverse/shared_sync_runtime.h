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
#include "apptraverse/share_offer.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse {

// One replica's shared synchronization runtime: it routes opaque transport
// bytes to the SharedNode named by the frame, admits an offered node, and
// owns retry of that exchange.
//
// Protocol v1 carries ShareOffer, ShareDecision, NodeState, Ack, and
// standalone Event. Dynamic object graphs and presence are later.
//
// Instance-scoped: the runtime holds its replica's Domain, storage, and
// transport. Nothing is process-global or thread_local, and no model pointer
// ever crosses a replica boundary — only bytes do.
//
// Logical retry clock. Tests advance time and call Service. Not wall time.
inline constexpr std::uint64_t kShareOfferRetryIntervalUs = 1'000'000;

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
  // the node ObjId in advance. Keyed by (source_endpoint, expected_node_id):
  // a second node from the same endpoint is a second expectation, not an
  // overwrite. An empty expected_node_id is the single wildcard slot for
  // that endpoint (chat admission that learns the ObjId from the snapshot).
  void ExpectInitialNodeFromEndpoint(std::string source_endpoint,
                                     std::uint32_t expected_root_class_id,
                                     ae::ObjId expected_node_id = {});

  // Remove every expectation for this endpoint. Does not unregister nodes.
  void ForgetInitialNodeFromEndpoint(std::string const& source_endpoint);

  // Remove one node expectation. Other nodes from the same endpoint stay.
  void ForgetInitialNodeExpectation(std::string const& source_endpoint,
                                    ae::ObjId node_id);

  using InitialNodeImportedCallback =
      std::function<bool(std::string const& source_endpoint,
                         SharedNode::ptr node)>;

  void SetInitialNodeImportedCallback(InitialNodeImportedCallback callback);

  // What the responder's policy sees. source_endpoint is the transport
  // source, not a field the offer frame can set.
  struct ShareOfferView {
    std::string source_endpoint;
    ae::ObjId operation_id;
    ae::ObjId node_id;
    std::uint32_t root_class_id{0};
    ShareAccess access{ShareAccess::ReadWrite};
  };

  using ShareOfferPolicy = std::function<bool(ShareOfferView const& offer)>;

  // Consulted only for an operation this replica has not already recorded.
  // Runtime-only: set again after restart. The recorded decision is what
  // survives.
  void SetShareOfferPolicy(ShareOfferPolicy policy);

  // Offer `node` to `remote`'s endpoint with `access` for that peer.
  // The node must already share this replica's own endpoint. The remote
  // Share is added only after the peer accepts.
  // Repeating the call for the same node and endpoint while an offer is
  // open returns the existing operation id.
  ae::ObjId OfferNode(SharedNode::ptr node, Link::ptr remote,
                      ShareAccess access);

  // Reload a ShareOffer from this replica's storage after a restart.
  // Attaches the named SharedNode when that node is already stored.
  void RegisterOffer(ShareOffer::ptr offer);

  struct ShareOfferStatus {
    ae::ObjId object_id;
    ae::ObjId operation_id;
    ae::ObjId node_id;
    std::string remote_endpoint;
    ShareOfferRole role{ShareOfferRole::Initiator};
    ShareOfferPhase phase{ShareOfferPhase::Unset};
    ShareAccess access{ShareAccess::ReadWrite};
  };

  std::vector<ShareOfferStatus> OfferStatuses() const;
  ShareOfferPhase OfferPhase(ae::ObjId operation_id) const;

  // ObjIds of ShareOffer objects, so a restart can reload them from storage.
  std::vector<ae::ObjId> LocalOfferIds() const;
  std::vector<ae::ObjId> RegisteredNodeIds() const;

  // Resend due offer/decision packets and drive initial plus incremental
  // sync for every registered node. `now_us` is the caller's logical clock.
  void Service(std::uint64_t now_us);

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
  void OnShareOffer(std::string const& source_endpoint,
                    ShareOfferFrame const& frame);
  void OnShareDecision(std::string const& source_endpoint,
                       ShareDecisionFrame const& frame);

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

  // Index into expected_endpoint_nodes_, or size() when none matches.
  // Exact node id wins over the empty-id wildcard.
  std::size_t MatchEndpointExpectation(std::string const& source_endpoint,
                                       ae::ObjId node_id) const;

  ShareOffer::ptr FindOfferByOperation(ae::ObjId operation_id) const;
  ShareOffer::ptr FindResponderAdmission(std::string const& source_endpoint,
                                         ae::ObjId node_id) const;

  void CommitOfferPhase(ShareOffer::ptr offer, ShareOfferPhase phase,
                        ae::ObjId share_id);
  void SaveOffer(ShareOffer::ptr offer);
  void NoteDirectSend(ae::ObjId operation_id);
  void NoteDirectSync(ae::ObjId node_id, ae::ObjId share_id);
  void MarkInitiatorComplete(ae::ObjId node_id, ae::ObjId share_id);
  bool InitiatorDrivesShare(ae::ObjId node_id, ae::ObjId share_id) const;
  void ServiceOffers(std::uint64_t now_us);
  void ServiceShares(std::uint64_t now_us);

  struct EndpointExpectation {
    std::string source_endpoint;
    std::uint32_t expected_root_class_id{0};
    ae::ObjId expected_node_id;
  };

  // Retry clocks are runtime-only. Frozen bytes live on the persisted objects.
  struct LiveOffer {
    ShareOffer::ptr offer;
    std::uint64_t next_send_us{0};
    bool primed{false};
  };

  struct SyncSlot {
    ae::ObjId node_id;
    ae::ObjId share_id;
    std::uint64_t next_us{0};
    bool primed{false};
  };

  static constexpr std::uint64_t kScheduleOnNextService =
      ~std::uint64_t{0};

  ae::Domain& domain_;
  ae::IDomainStorage& storage_;
  IByteTransport& transport_;
  std::vector<SharedNode::ptr> nodes_;
  std::vector<ae::ObjId> expected_initial_nodes_;
  std::vector<EndpointExpectation> expected_endpoint_nodes_;
  std::vector<LiveOffer> offers_;
  std::vector<SyncSlot> sync_slots_;
  InitialNodeImportedCallback initial_node_imported_callback_;
  ShareOfferPolicy share_offer_policy_;
  std::vector<std::uint32_t> standalone_event_classes_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
