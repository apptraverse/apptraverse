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
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse {

// One replica's shared synchronization runtime: it routes opaque transport
// bytes to the SharedNode named by the frame, admits an offered node, and
// owns retry of that exchange.
//
// Protocol v1 carries ShareOffer, ShareDecision, NodeState, Ack, Event,
// and ShareCatchUp. Topology changes travel as shared Events. Outgoing
// endpoint availability is read from the transport. Arbitrary dynamic
// object graphs, heartbeat, and real Æther presence are later.
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
    ShareOfferKind kind{ShareOfferKind::Grant};
  };

  using ShareOfferPolicy = std::function<bool(ShareOfferView const& offer)>;

  // Fired when an inbound grant or request is stored and still unanswered.
  // Also fired for each such attempt when the notice is set, so a new
  // runtime surfaces attempts loaded from storage. Not a decision.
  using ShareOfferNotice = std::function<void(ShareOfferView const& offer)>;

  // Consulted only when an inbound attempt is first stored. A saved decision
  // is not asked again. No policy means the attempt stays AwaitingDecision.
  void SetShareOfferPolicy(ShareOfferPolicy policy);
  void SetShareOfferNotice(ShareOfferNotice notice);

  // Builds the Share Link for a request this replica accepts. The runtime
  // does not construct a transport-specific Link itself.
  using LinkForEndpoint = std::function<Link::ptr(std::string const& endpoint)>;
  void SetLinkForEndpoint(LinkForEndpoint link_for_endpoint);

  // Posted when endpoint availability changes. The callback must not send:
  // it only wakes the model loop, which then calls Service. A repeated
  // Online with no change does not call it.
  using AvailabilityWake = std::function<void()>;
  void SetAvailabilityWake(AvailabilityWake wake);

  // Offer `node` to `remote`'s endpoint with `access` for that peer.
  // The node must already share this replica's own endpoint. The remote
  // Share is added only after the peer accepts.
  // Repeating the call for the same node and endpoint while an offer is
  // open returns the existing operation id.
  ae::ObjId OfferNode(SharedNode::ptr node, Link::ptr remote,
                      ShareAccess access);

  // Ask `remote_endpoint` for a node this replica does not have.
  // Does not create a local copy. The holder sends the snapshot after accept.
  ae::ObjId RequestJoin(std::string remote_endpoint, ae::ObjId node_id,
                        ShareAccess requested_access);

  // Answer a stored AwaitingDecision attempt. `remote` is the Share Link the
  // holder adds; a grant does not need one because the share arrives in the
  // snapshot. Ignored when the attempt is not waiting.
  void AcceptJoin(ae::ObjId operation_id, ShareAccess granted_access,
                  Link::ptr remote = {});
  void RejectJoin(ae::ObjId operation_id);

  // Publish a topology change on a node this replica already holds. The
  // event is shared, so connected replicas learn it. share_id is the
  // existing relationship, not a new one.
  void ChangeShareAccess(ae::ObjId node_id, ae::ObjId share_id,
                         ShareAccess access);
  void RemoveShare(ae::ObjId node_id, ae::ObjId share_id);

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
  void SyncCatchUp(ae::ObjId node_id, ae::ObjId share_id);

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
  static void AvailabilityThunk(void* ctx, std::string const& endpoint,
                                EndpointAvailability availability);

  void OnAvailability(std::string const& endpoint,
                      EndpointAvailability availability);

  void OnBytes(std::string const& source_endpoint,
               std::vector<std::uint8_t> const& bytes);
  void OnNodeState(std::string const& source_endpoint,
                   NodeStateFrame const& frame);
  void OnAck(std::string const& source_endpoint, AckFrame const& frame);
  void OnEvent(std::string const& source_endpoint, EventFrame const& frame);
  void OnCatchUp(std::string const& source_endpoint,
                 ShareCatchUpFrame const& frame);
  void OnShareOffer(std::string const& source_endpoint,
                    ShareOfferFrame const& frame);
  void OnShareRequest(std::string const& source_endpoint,
                      ShareOfferFrame const& frame);
  void OnAdmission(std::string const& source_endpoint,
                   ShareOfferFrame const& frame, ShareOfferKind kind);
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
  ShareOffer::ptr FindImportAdmission(std::string const& source_endpoint,
                                      ae::ObjId node_id) const;
  bool OpenAttemptBlocks(std::string const& remote_endpoint,
                         ae::ObjId node_id) const;
  bool AttemptMatches(ShareOffer const& offer, std::string const& source,
                      ShareOfferFrame const& frame,
                      ShareOfferKind kind) const;
  bool SendsSnapshot(ShareOffer const& offer) const;
  bool DrivesInitialSnapshot(ae::ObjId node_id, ae::ObjId share_id) const;
  ShareOfferView ViewOf(ShareOffer const& offer) const;

  void CommitOfferPhase(ShareOffer::ptr offer, ShareOfferPhase phase,
                        ae::ObjId share_id,
                        std::vector<std::uint8_t> packet = {},
                        std::uint8_t access = 0xFF,
                        std::uint32_t root_class_id = 0);
  void SaveOffer(ShareOffer::ptr offer);
  void RememberOffer(ShareOffer::ptr offer, bool send_now);
  void LoadAdmission();
  void NoteDirectSend(ae::ObjId operation_id);
  void NoteDirectSync(ae::ObjId node_id, ae::ObjId share_id);
  void MarkDecisionUnsent(ae::ObjId operation_id);
  void ArmEndpoint(std::string const& endpoint);
  bool OutgoingOffline(std::string const& endpoint) const;
  // Hands bytes to the transport only when the endpoint is not Offline.
  // False means nothing was sent: the caller keeps the persisted packet.
  bool TrySend(std::string const& endpoint,
               std::vector<std::uint8_t> const& bytes);
  void QueueAck(std::string const& endpoint, AckFrame const& frame);
  void ServiceAcks();
  void MarkSnapshotSenderComplete(ae::ObjId node_id, ae::ObjId share_id);
  void ServiceOffers(std::uint64_t now_us);
  void ServiceShares(std::uint64_t now_us);

  SharedEventId AllocateSharedIdentity();
  SharedEventOrder AllocateSharedOrder(SharedNode const& node);
  void PublishAddShare(SharedNode::ptr node, Link::ptr link,
                       ShareAccess access);
  void PublishRemoveShare(SharedNode::ptr node, ae::ObjId share_id);
  void PublishShareAccess(SharedNode::ptr node, ae::ObjId share_id,
                          ShareAccess access);
  // The removed relationship is already gone from shares, so the ordinary
  // per-share sender cannot address it. The journal event is the record;
  // this only retries the packet until the removed endpoint acknowledges.
  void RelayRemovedShare(SharedNode::ptr node, ae::ObjId share_id);
  void RelayAppliedRemovals(SharedNode::ptr node);
  void ServiceRelays(std::uint64_t now_us);
  // Existing replica rejoining: fold missing shared journal events from a
  // snapshot without replacing the local SharedNode graph.
  bool FoldMissingSharedFromSnapshot(SharedNode::ptr node,
                                     std::string const& source_endpoint,
                                     NodeStateFrame const& frame,
                                     ShareOffer::ptr admission);
  bool IsTopologyEventClass(std::uint32_t class_id) const;
  bool LocalShareAllowsWrite(SharedNode const& node) const;
  bool ApplyIncomingAddShare(SharedNode::ptr node,
                             std::string const& source_endpoint,
                             EventFrame const& frame);

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
    // Decision bytes are already on the offer. Set when they have not been
    // handed to the transport yet, so Service can send that same packet
    // after Offline without a second outbox.
    bool decision_unsent{false};
  };

  struct SyncSlot {
    ae::ObjId node_id;
    ae::ObjId share_id;
    std::uint64_t next_us{0};
    bool primed{false};
  };

  struct ObservedAvailability {
    std::string endpoint;
    EndpointAvailability availability{EndpointAvailability::Unknown};
  };

  // Runtime-only. Not persisted: after restart the peer's retry reproduces
  // the ACK from the saved received packet id.
  struct PendingAck {
    std::string endpoint;
    std::vector<std::uint8_t> bytes;
  };

  static constexpr std::uint64_t kScheduleOnNextService =
      ~std::uint64_t{0};

  ae::Domain& domain_;
  ae::IDomainStorage& storage_;
  IByteTransport& transport_;
  ShareAdmission::ptr admission_;
  std::vector<SharedNode::ptr> nodes_;
  std::vector<ae::ObjId> expected_initial_nodes_;
  std::vector<EndpointExpectation> expected_endpoint_nodes_;
  std::vector<LiveOffer> offers_;
  std::vector<SyncSlot> sync_slots_;
  InitialNodeImportedCallback initial_node_imported_callback_;
  ShareOfferPolicy share_offer_policy_;
  ShareOfferNotice share_offer_notice_;
  LinkForEndpoint link_for_endpoint_;
  AvailabilityWake availability_wake_;
  std::vector<std::uint32_t> standalone_event_classes_;
  std::vector<ObservedAvailability> observed_availability_;
  std::vector<PendingAck> pending_acks_;
  // Advanced by Service when the caller moves time forward. Topology commits
  // take the next tick so their order is the order they were published.
  std::uint64_t logical_now_us_{0};
  std::uint64_t next_shared_sequence_{1};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
