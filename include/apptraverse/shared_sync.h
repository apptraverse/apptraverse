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

enum class SharedSyncKind : std::uint8_t { InitialState = 1, Event = 2, Ack = 3 };

// The entire message, including native object layers, uses BinaryArchive.
// No opaque custom graph encoding is hidden inside the payload.
struct SharedSyncMessage {
  std::uint16_t major{1};
  std::uint16_t minor{0};
  SharedSyncKind kind{SharedSyncKind::Event};
  SharedSyncKind acknowledged_kind{SharedSyncKind::Event};
  ae::ObjId node_id;
  ae::ObjId packet_id;
  ae::ObjId event_id;
  std::uint64_t timestamp_us{0};
  SharedGraphSnapshot graph;
  std::vector<ae::ObjId> covered_events;

  AE_REFLECT_MEMBERS(major, minor, kind, acknowledged_kind, node_id, packet_id,
                    event_id, timestamp_us, graph, covered_events)
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

  // Both methods persist frozen outgoing packets but do not send them. Service
  // drives actual transmission, enabling destruction after persist/before Send.
  bool QueueInitialState(ae::ObjId node_id, std::string const& destination);
  bool CommitLocal(ae::ObjId node_id, Event::ptr event,
                    std::uint64_t timestamp_us = 0);
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
  bool QueueLocalEvents(SharedNode& node,
                         SharedGraphSnapshot const* newly_frozen = nullptr);
  void OnBytes(std::string const& source, std::vector<std::uint8_t> const& bytes);
  bool OnInitial(std::string const& source, SharedSyncMessage const& message);
  bool OnEvent(std::string const& source, SharedSyncMessage const& message);
  bool OnAck(std::string const& source, SharedSyncMessage const& message);
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
