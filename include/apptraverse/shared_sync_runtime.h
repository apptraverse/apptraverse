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
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse {

// One replica's shared synchronization runtime for permanent AeroAdmin 1:1
// dialogs. Routes opaque transport bytes to the SharedNode named by the frame,
// admits an expected initial NodeState, and owns retry of that exchange.
//
// Protocol v1 carries NodeState, Ack, and Event. One SharedNode has exactly
// two permanent ReadWrite shares after connect. Topology mutation (Offer,
// Remove, ChangeAccess, CatchUp, rejoin fold) is not part of this surface.
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

  void RegisterNode(SharedNode::ptr node);
  void ExpectInitialNode(ae::ObjId node_id);
  void ExpectInitialNodeFromEndpoint(std::string source_endpoint,
                                     std::uint32_t expected_root_class_id,
                                     ae::ObjId expected_node_id = {});
  void ForgetInitialNodeFromEndpoint(std::string const& source_endpoint);
  void ForgetInitialNodeExpectation(std::string const& source_endpoint,
                                    ae::ObjId node_id);

  using InitialNodeImportedCallback =
      std::function<bool(std::string const& source_endpoint,
                         SharedNode::ptr node)>;
  void SetInitialNodeImportedCallback(InitialNodeImportedCallback callback);

  using AvailabilityWake = std::function<void()>;
  void SetAvailabilityWake(AvailabilityWake wake);

  void Service(std::uint64_t now_us);
  SharedNode::ptr FindNode(ae::ObjId node_id) const;
  void SyncInitialState(ae::ObjId node_id, ae::ObjId share_id);
  void SyncNextEvent(ae::ObjId node_id, ae::ObjId share_id);
  void AllowStandaloneEventClass(std::uint32_t class_id);
  std::vector<ae::ObjId> RegisteredNodeIds() const;

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

  struct ImportedNode {
    SharedNode::ptr node;
    ae::ObjId source_share_id;
  };

  ImportedNode ImportValidatedNode(std::string const& source_endpoint,
                                   NodeStateFrame const& frame);
  bool IsExpectedInitialNode(ae::ObjId node_id) const;
  std::size_t MatchEndpointExpectation(std::string const& source_endpoint,
                                       ae::ObjId node_id) const;
  void ArmEndpoint(std::string const& endpoint);
  bool OutgoingOffline(std::string const& endpoint) const;
  bool TrySend(std::string const& endpoint,
               std::vector<std::uint8_t> const& bytes);
  void QueueAck(std::string const& endpoint, AckFrame const& frame);
  void ServiceAcks();
  void ServiceShares(std::uint64_t now_us);

  bool LocalShareAllowsWrite(SharedNode const& node) const;

  struct EndpointExpectation {
    std::string source_endpoint;
    std::uint32_t expected_root_class_id{0};
    ae::ObjId expected_node_id;
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

  struct PendingAck {
    std::string endpoint;
    std::vector<std::uint8_t> bytes;
  };

  ae::Domain& domain_;
  ae::IDomainStorage& storage_;
  IByteTransport& transport_;
  std::vector<SharedNode::ptr> nodes_;
  std::vector<ae::ObjId> expected_initial_nodes_;
  std::vector<EndpointExpectation> expected_endpoint_nodes_;
  std::vector<SyncSlot> sync_slots_;
  InitialNodeImportedCallback initial_node_imported_callback_;
  AvailabilityWake availability_wake_;
  std::vector<std::uint32_t> standalone_event_classes_;
  std::vector<ObservedAvailability> observed_availability_;
  std::vector<PendingAck> pending_acks_;
  std::uint64_t logical_now_us_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_SYNC_RUNTIME_H_
