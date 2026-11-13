#ifndef APPTRAVERSE_SHARED_SYNC_H_
#define APPTRAVERSE_SHARED_SYNC_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "apptraverse/memory_link.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/shared_node.h"

namespace apptraverse {

enum class SharedSyncKind : std::uint8_t {
  InitialState = 1, Event = 2, Ack = 3, CoverageReceipt = 4, Progress = 5
};

// The entire message, including native object layers, uses BinaryArchive.
// No opaque custom graph encoding is hidden inside the payload.
struct SharedSyncMessage {
  std::uint16_t major{1};
  std::uint16_t minor{3};
  SharedSyncKind kind{SharedSyncKind::Event};
  SharedSyncKind acknowledged_kind{SharedSyncKind::Event};
  ae::ObjId node_id;
  ae::ObjId packet_id;
  ae::ObjId event_id;
  std::uint64_t timestamp_us{0};
  SharedGraphSnapshot graph;
  std::vector<ae::ObjId> covered_events;
  SharedAdmissionPlan admission;
  // Event identity, snapshot receipt ledger, and cumulative Event ACK cursor.
  // These fields use the same native archive as the graph; no custom framing.
  SharedEventStamp stamp;
  std::vector<SharedEventStamp> stream_events;
  SharedStreamCursor received_prefix;
  SharedProgressMarker progress;
  std::uint64_t checkpoint_us{0};

  AE_REFLECT_MEMBERS(major, minor, kind, acknowledged_kind, node_id, packet_id,
                    event_id, timestamp_us, graph, covered_events, admission,
                    stamp, stream_events, received_prefix, progress, checkpoint_us)
};

bool SerializeSharedSyncMessage(SharedSyncMessage const& message,
                                std::vector<std::uint8_t>& bytes);
bool DeserializeSharedSyncMessage(std::vector<std::uint8_t> const& bytes,
                                  SharedSyncMessage& message);

// Model-thread runtime. Applications own their Domain/storage and persist the
// whole local Application graph in Persist. Incoming initial roots are attached
// by the explicit callback BEFORE Persist and BEFORE their ACK is emitted.
class SharedSyncRuntime {
 public:
  using Persist = std::function<bool()>;
  using AttachInitial = std::function<void(SharedNode::ptr)>;

  SharedSyncRuntime(ae::Domain& domain, ae::IDomainStorage& storage,
                    IByteLink& link, std::string local_endpoint,
                    Persist persist, std::uint64_t retry_interval_us = 1000000);
  ~SharedSyncRuntime();
  SharedSyncRuntime(SharedSyncRuntime const&) = delete;
  SharedSyncRuntime& operator=(SharedSyncRuntime const&) = delete;

  // Explicit bootstrap for fresh or imported shared roots. The local subtree
  // is prepared once, rather than reconstructed by shared replay or reconnect.
  static void InitializeLocalState(SharedNode& node);
  void RegisterNode(SharedNode::ptr node);
  SharedNode::ptr FindNode(ae::ObjId node_id) const;
  void ExpectInitialState(ae::ObjId node_id, std::string source_endpoint,
                          std::uint32_t expected_class_id, AttachInitial attach);

  // These entry points persist frozen packets but do not send them. Service
  // drives actual transmission, enabling destruction after persist/before Send.
  bool QueueInitialState(ae::ObjId node_id, std::string const& destination);
  // The application Join Event carries the participant's native graph. Its
  // Apply must introduce joining_endpoint in node.links. Join, snapshot and
  // delivery obligations are frozen/persisted before any transmission.
  bool AdmitPeer(ae::ObjId node_id, Event::ptr join_event,
                   std::string joining_endpoint, std::uint64_t timestamp_us = 0);
  // Explicit timestamps must strictly increase for this author and exceed the
  // adopted snapshot floor. Zero selects a durable-monotonic automatic time.
  bool CommitLocal(ae::ObjId node_id, Event::ptr event,
                    std::uint64_t timestamp_us = 0);
  // Persist a promise closing this author's past and broadcast durable receipt
  // evidence. Explicit refresh is allowed at the same time after new receipts.
  bool PublishProgress(ae::ObjId node_id, std::uint64_t through_timestamp_us = 0);
  // Node-owned hold is opened only for a prefix proven closed and received by
  // every member. The native compactor may conservatively refuse that prefix.
  bool CompactConfirmed(ae::ObjId node_id);
  void Service(std::uint64_t now_us);
  bool failed() const { return failed_; }
  std::size_t rejected_messages() const { return rejected_messages_; }

 private:
  struct InitialExpectation {
    std::string source;
    std::uint32_t class_id;
    AttachInitial attach;
  };
  using RetryKey = std::tuple<ae::ObjId, std::string, ae::ObjId>;

  SharedPeerState::ptr FindPeer(SharedNode const& node,
                                std::string const& endpoint) const;
  bool PersistState();
  bool QueueLocalEvents(SharedNode& node);
  bool QueueCoverageReceipts(SharedNode& node, SharedAdmissionPlan const& plan);
  void ReconcilePeers(SharedNode& node);
  bool InstallAdmission(SharedNode& node, SharedAdmissionPlan const& plan);
  void OnBytes(std::string const& source, std::vector<std::uint8_t> const& bytes);
  bool OnInitial(std::string const& source, SharedSyncMessage const& message);
  bool OnEvent(std::string const& source, SharedSyncMessage const& message);
  bool OnAck(std::string const& source, SharedSyncMessage const& message);
  bool OnCoverage(std::string const& source, SharedSyncMessage const& message);
  bool OnProgress(std::string const& source, SharedSyncMessage const& message);
  void SendAck(std::string const& destination, SharedSyncMessage const& original);

  ae::Domain& domain_;
  ae::IDomainStorage& storage_;
  IByteLink& link_;
  std::string local_endpoint_;
  Persist persist_;
  std::uint64_t retry_interval_us_;
  std::uint64_t now_us_{0};
  std::map<ae::ObjId, SharedNode::ptr> nodes_;
  std::map<ae::ObjId, InitialExpectation> expected_;
  std::map<RetryKey, std::uint64_t> next_send_;
  bool failed_{false};
  std::size_t rejected_messages_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_SYNC_H_
