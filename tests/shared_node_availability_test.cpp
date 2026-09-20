#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/byte_transport.h"
#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/share_offer.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::string const kA = "endpoint-a";
std::string const kB = "endpoint-b";
std::string const kC = "endpoint-c";
std::string const kSecret = "AVAIL_LOCAL_SECRET_do_not_ship";

class AvailLocalSecret : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AvailLocalSecret",
                           AvailLocalSecret, ae::Obj, 0)

 protected:
  AvailLocalSecret() = default;

 public:
  explicit AvailLocalSecret(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(mark))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, mark);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, mark);
  }
  std::string mark;
};

class AddAvailRecordEvent;
class AvailRecordNode
    : public apptraverse::NodeFor<AvailRecordNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AvailRecordNode", AvailRecordNode,
                           SharedNode, 0)

 protected:
  AvailRecordNode() = default;

 public:
  explicit AvailRecordNode(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(local_secret))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, records, local_secret);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, records, local_secret);
  }
  std::vector<std::string> records;
  apptraverse::LocalPtr<AvailLocalSecret> local_secret;
  void Apply(AddAvailRecordEvent const& event);
};

class AddAvailRecordEvent
    : public apptraverse::EventFor<AvailRecordNode, AddAvailRecordEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AddAvailRecordEvent",
                           AddAvailRecordEvent, Event, 0)

 protected:
  AddAvailRecordEvent() = default;

 public:
  explicit AddAvailRecordEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }
  std::string text;
};

void AvailRecordNode::Apply(AddAvailRecordEvent const& event) {
  records.push_back(event.text);
  NoteMaterializedChange();
}

APPTRAVERSE_REGISTER(AvailLocalSecret);
APPTRAVERSE_REGISTER(AvailRecordNode);
APPTRAVERSE_REGISTER(AddAvailRecordEvent);

std::uint64_t g_now = 0;

class SendProbe final : public IByteTransport {
 public:
  explicit SendProbe(MemoryTransport& inner) : inner_{inner} {}

  std::string const& local_endpoint_uid() const override {
    return inner_.local_endpoint_uid();
  }

  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override {
    ++sends;
    ++by_dest[destination_endpoint];
    last = bytes;
    inner_.Send(destination_endpoint, std::move(bytes));
  }

  void BindReceive(void* ctx, ReceiveFn fn) override {
    inner_.BindReceive(ctx, fn);
  }
  void ClearReceive() override { inner_.ClearReceive(); }

  EndpointAvailability Availability(std::string const& endpoint) const override {
    return inner_.Availability(endpoint);
  }
  void BindAvailability(void* ctx, AvailabilityFn fn) override {
    inner_.BindAvailability(ctx, fn);
  }
  void ClearAvailability() override { inner_.ClearAvailability(); }

  std::uint64_t sends{0};
  std::map<std::string, std::uint64_t> by_dest;
  std::vector<std::uint8_t> last;

 private:
  MemoryTransport& inner_;
};

// Adapter with a caller-drained queue. Network notifications land here and
// do not enter SharedSyncRuntime until Drain, which is the model context.
class QueuedTransport final : public IByteTransport {
 public:
  explicit QueuedTransport(MemoryTransport& inner) : inner_{inner} {}

  std::string const& local_endpoint_uid() const override {
    return inner_.local_endpoint_uid();
  }

  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override {
    ++sends;
    inner_.Send(destination_endpoint, std::move(bytes));
  }

  void BindReceive(void* ctx, ReceiveFn fn) override {
    receive_ctx_ = ctx;
    receive_fn_ = fn;
    inner_.BindReceive(this, &QueueReceive);
  }
  void ClearReceive() override {
    receive_ctx_ = nullptr;
    receive_fn_ = nullptr;
    receives_.clear();
    inner_.ClearReceive();
  }

  EndpointAvailability Availability(std::string const& endpoint) const override {
    auto const it = published_.find(endpoint);
    if (it == published_.end()) {
      return EndpointAvailability::Unknown;
    }
    return it->second;
  }
  void BindAvailability(void* ctx, AvailabilityFn fn) override {
    availability_ctx_ = ctx;
    availability_fn_ = fn;
    inner_.BindAvailability(this, &QueueAvailability);
  }
  void ClearAvailability() override {
    availability_ctx_ = nullptr;
    availability_fn_ = nullptr;
    availability_notes_.clear();
    inner_.ClearAvailability();
  }

  void Drain() {
    auto availability = std::move(availability_notes_);
    availability_notes_.clear();
    for (auto const& note : availability) {
      if (availability_fn_ == nullptr) {
        break;
      }
      published_[note.endpoint] = note.availability;
      ++availability_deliveries;
      availability_fn_(availability_ctx_, note.endpoint, note.availability);
    }
    auto receives = std::move(receives_);
    receives_.clear();
    for (auto const& note : receives) {
      if (receive_fn_ == nullptr) {
        break;
      }
      ++receive_deliveries;
      receive_fn_(receive_ctx_, note.source, note.bytes);
    }
  }

  std::size_t queued_availability() const { return availability_notes_.size(); }
  std::size_t queued_receives() const { return receives_.size(); }

  std::uint64_t sends{0};
  std::uint64_t availability_deliveries{0};
  std::uint64_t receive_deliveries{0};

 private:
  struct AvailNote {
    std::string endpoint;
    EndpointAvailability availability{EndpointAvailability::Unknown};
  };
  struct RecvNote {
    std::string source;
    std::vector<std::uint8_t> bytes;
  };

  static void QueueAvailability(void* ctx, std::string const& endpoint,
                                EndpointAvailability availability) {
    static_cast<QueuedTransport*>(ctx)->availability_notes_.push_back(
        AvailNote{endpoint, availability});
  }
  static void QueueReceive(void* ctx, std::string const& source,
                           std::vector<std::uint8_t> const& bytes) {
    static_cast<QueuedTransport*>(ctx)->receives_.push_back(
        RecvNote{source, bytes});
  }

  MemoryTransport& inner_;
  void* receive_ctx_{nullptr};
  ReceiveFn receive_fn_{nullptr};
  void* availability_ctx_{nullptr};
  AvailabilityFn availability_fn_{nullptr};
  std::map<std::string, EndpointAvailability> published_;
  std::vector<AvailNote> availability_notes_;
  std::vector<RecvNote> receives_;
};

struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint_uid)
      : storage{},
        network_{network},
        endpoint_uid_{std::move(endpoint_uid)} {}

  void Start(bool accept_policy) {
    binds = 0;
    wakes = 0;
    sends_at_wake = 0;
    domain = std::make_unique<ae::Domain>(storage);
    transport = std::make_unique<MemoryTransport>(network_, endpoint_uid_);
    probe = std::make_unique<SendProbe>(*transport);
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, *probe);
    sync->AllowStandaloneEventClass(AddAvailRecordEvent::kClassId);
    if (accept_policy) {
      sync->SetShareOfferPolicy(
          [](SharedSyncRuntime::ShareOfferView const&) { return true; });
    }
    sync->SetLinkForEndpoint([this](std::string const& endpoint) {
      return MakeLink(endpoint);
    });
    sync->SetAvailabilityWake([this] {
      ++wakes;
      sends_at_wake = probe->sends;
    });
    sync->SetInitialNodeImportedCallback(
        [this](std::string const&, SharedNode::ptr node) {
          ++binds;
          return Bind(node);
        });
  }

  void Stop() {
    sync.reset();
    probe.reset();
    transport.reset();
    domain.reset();
  }

  std::string const& endpoint() const { return endpoint_uid_; }
  std::uint64_t sends() const { return probe->sends; }
  std::uint64_t sent_to(std::string const& endpoint) const {
    auto const it = probe->by_dest.find(endpoint);
    return it == probe->by_dest.end() ? 0 : it->second;
  }

  MemoryLink::ptr MakeLink(std::string endpoint) {
    auto link = MemoryLink::ptr::Create(
        ae::CreateWith{*domain}.with_id(ae::ObjId::GenerateUnique()));
    link->endpoint_uid = std::move(endpoint);
    link->heartbeat_interval_ms = 1000;
    InitializeRuntimeNode(*link);
    link.Save();
    return link;
  }

  bool Bind(SharedNode::ptr node) {
    node.Save();
    return true;
  }

  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<SendProbe> probe;
  std::unique_ptr<SharedSyncRuntime> sync;
  int binds{0};
  int wakes{0};
  std::uint64_t sends_at_wake{0};

 private:
  MemoryNetwork& network_;
  std::string endpoint_uid_;
};

bool Contains(std::vector<std::uint8_t> const& bytes, std::string const& needle) {
  if (needle.empty() || bytes.size() < needle.size()) {
    return false;
  }
  std::string_view const view(reinterpret_cast<char const*>(bytes.data()),
                              bytes.size());
  return view.find(needle) != std::string_view::npos;
}

AvailRecordNode::ptr AsRecord(SharedNode::ptr node) {
  AvailRecordNode::ptr concrete = node;
  concrete.Load();
  CHECK(concrete.is_loaded());
  return concrete;
}

AvailRecordNode::ptr MakeRecord(Replica& replica, std::string text,
                                std::string secret) {
  auto node = AvailRecordNode::ptr::Create(ae::CreateWith{*replica.domain});
  InitializeRuntimeNode(*node);
  auto hidden = AvailLocalSecret::ptr::Create(ae::CreateWith{*replica.domain});
  hidden->mark = std::move(secret);
  hidden.Save();
  node->local_secret = hidden;
  auto self = replica.MakeLink(replica.endpoint());
  node->AddShare(self, ShareAccess::ReadWrite);
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
  replica.sync->RegisterNode(node);
  if (!text.empty()) {
    auto event =
        AddAvailRecordEvent::ptr::Create(ae::CreateWith{*replica.domain});
    event->text = std::move(text);
    node->CommitShared(std::move(event),
                       SharedEventId{.origin_uid = replica.endpoint(),
                                     .origin_sequence = 1},
                       SharedEventOrder{.timestamp_us = 1000});
    node.Save();
  }
  return node;
}

void AddRecord(AvailRecordNode& node, std::string text, std::string origin,
               std::uint64_t sequence, std::uint64_t timestamp_us) {
  auto event = AddAvailRecordEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->text = text;
  node.CommitShared(std::move(event),
                    SharedEventId{.origin_uid = std::move(origin),
                                  .origin_sequence = sequence},
                    SharedEventOrder{.timestamp_us = timestamp_us});
  AvailRecordNode::ptr::MakeFromThis(&node).Save();
}

struct Seen {
  SharedEventId id;
  std::uint64_t timestamp_us{0};
  std::string text;
};

std::vector<Seen> Observe(AvailRecordNode const& node) {
  std::vector<Seen> out;
  for (auto const& record : node.journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (event->GetClassId() != AddAvailRecordEvent::kClassId) {
      continue;
    }
    AddAvailRecordEvent::ptr concrete = event;
    CHECK(concrete.is_loaded());
    out.push_back(Seen{.id = record.identity,
                       .timestamp_us = record.order.timestamp_us,
                       .text = concrete->text});
  }
  return out;
}

void ExpectPayload(SharedNode::ptr node, std::string const& text,
                   std::string const& origin, std::uint64_t sequence) {
  auto concrete = AsRecord(node);
  CHECK(!concrete->local_secret.is_valid());
  auto const seen = Observe(*concrete);
  CHECK(!seen.empty());
  bool found = false;
  for (auto const& item : seen) {
    if (item.text == text && item.id.origin_uid == origin &&
        item.id.origin_sequence == sequence) {
      found = true;
      CHECK(item.timestamp_us != 0);
    }
  }
  CHECK(found);
  CHECK(concrete->records.size() == seen.size());
}

struct World {
  MemoryNetwork* network{nullptr};
  std::vector<Replica*> replicas;
};

bool Deliver(World& world) {
  bool any = false;
  for (auto* from : world.replicas) {
    for (auto* to : world.replicas) {
      if (from == to) {
        continue;
      }
      auto const& bytes =
          world.network->PeekNext(from->endpoint(), to->endpoint());
      if (Contains(bytes, kSecret)) {
        std::cerr << "secret leaked " << from->endpoint() << " -> "
                  << to->endpoint() << '\n';
        CHECK(false);
      }
      if (world.network->DeliverNext(from->endpoint(), to->endpoint())) {
        any = true;
      }
    }
  }
  return any;
}

void Pump(World& world, auto&& done) {
  for (int step = 0; step < 48; ++step) {
    if (done()) {
      return;
    }
    Deliver(world);
    for (auto* replica : world.replicas) {
      replica->sync->Service(g_now);
    }
    if (done()) {
      return;
    }
  }
  std::cerr << "pump failed at t=" << g_now << '\n';
  CHECK(false);
}

void Silence(Replica& replica, MemoryNetwork& network, std::string const& to,
             std::uint64_t intervals) {
  auto const sends = replica.sends();
  auto const queued = network.PendingCount(replica.endpoint(), to);
  g_now += intervals * kShareOfferRetryIntervalUs;
  replica.sync->Service(g_now);
  CHECK(replica.sends() == sends);
  CHECK(network.PendingCount(replica.endpoint(), to) == queued);
}

void GoOnline(Replica& replica, MemoryNetwork& network, std::string const& to) {
  auto const sends = replica.sends();
  auto const wakes = replica.wakes;
  network.SetAvailability(replica.endpoint(), to, EndpointAvailability::Online);
  CHECK(replica.wakes == wakes + 1);
  CHECK(replica.sends() == sends);
  CHECK(replica.sends_at_wake == sends);
}

void Note(void* ctx, std::string const& endpoint, EndpointAvailability availability) {
  auto* note = static_cast<std::pair<int, EndpointAvailability>*>(ctx);
  ++note->first;
  note->second = availability;
  (void)endpoint;
}

void TestTransportAvailability() {
  MemoryNetwork network;
  CHECK(network.Availability(kA, kB) == EndpointAvailability::Unknown);
  CHECK(network.Availability(kB, kA) == EndpointAvailability::Unknown);

  MemoryTransport transport_a(network, kA);
  MemoryTransport transport_b(network, kB);
  std::pair<int, EndpointAvailability> note_a{0, EndpointAvailability::Unknown};
  std::pair<int, EndpointAvailability> note_b{0, EndpointAvailability::Unknown};
  transport_a.BindAvailability(&note_a, &Note);
  transport_b.BindAvailability(&note_b, &Note);

  network.SetAvailability(kA, kB, EndpointAvailability::Unknown);
  CHECK(note_a.first == 0);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(note_a.first == 1);
  CHECK(note_a.second == EndpointAvailability::Online);
  CHECK(note_b.first == 0);
  CHECK(transport_b.Availability(kA) == EndpointAvailability::Unknown);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(note_a.first == 1);

  network.SetAvailability(kB, kA, EndpointAvailability::Offline);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Online);
  CHECK(transport_b.Availability(kA) == EndpointAvailability::Offline);
  network.Disconnect(kA, kB);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Online);
  transport_a.Send(kB, std::vector<std::uint8_t>{1, 2, 3});
  CHECK(network.PendingCount(kA, kB) == 0);
  network.Reconnect(kA, kB);

  MemoryNetwork fresh;
  MemoryTransport restarted(fresh, kA);
  CHECK(restarted.Availability(kB) == EndpointAvailability::Unknown);
  CHECK(fresh.Availability(kA, kB) == EndpointAvailability::Unknown);
}

void TestInitialOfflineOffer() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(true);
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  auto node = MakeRecord(holder, "offer-seed", kSecret);
  auto const node_id = node.id();
  auto remote = holder.MakeLink(kB);
  auto const op =
      holder.sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Pending);
  CHECK(holder.sends() == 0);
  CHECK(holder.sent_to(kB) == 0);
  CHECK(network.PendingCount(kA, kB) == 0);
  Silence(holder, network, kB, 200);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Pending);

  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 1);
  auto const saved = holder.probe->last;
  ShareOfferFrame frame;
  CHECK(DecodeShareOfferFrame(saved, frame));
  CHECK(frame.operation_id == op);
  CHECK(frame.target_node_id == node_id);
  CHECK(network.PeekNext(kA, kB) == saved);

  World world{&network, {&holder, &peer}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op) == ShareOfferPhase::Complete &&
           peer.sync->FindNode(node_id).is_valid() &&
           peer.sync->OfferPhase(op) == ShareOfferPhase::Bound;
  });
  ExpectPayload(peer.sync->FindNode(node_id), "offer-seed", kA, 1);
  CHECK(peer.binds == 1);
  CHECK(holder.sync->FindNode(node_id).id() == node_id);
}

void TestInitialOfflineRequest() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(true);
  requester.Start(false);
  network.SetAvailability(kB, kA, EndpointAvailability::Offline);
  auto node = MakeRecord(holder, "request-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      requester.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(requester.sync->OfferPhase(op) == ShareOfferPhase::Pending);
  CHECK(requester.sends() == 0);
  CHECK(network.PendingCount(kB, kA) == 0);
  CHECK(!requester.sync->FindNode(node_id).is_valid());
  Silence(requester, network, kA, 200);

  GoOnline(requester, network, kA);
  requester.sync->Service(g_now);
  CHECK(requester.sends() == 1);
  ShareOfferFrame frame;
  CHECK(DecodeShareRequestFrame(requester.probe->last, frame));
  CHECK(frame.operation_id == op);
  CHECK(frame.target_node_id == node_id);

  World world{&network, {&holder, &requester}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op) == ShareOfferPhase::Complete &&
           requester.sync->OfferPhase(op) == ShareOfferPhase::Bound &&
           requester.sync->FindNode(node_id).is_valid();
  });
  ExpectPayload(requester.sync->FindNode(node_id), "request-seed", kA, 1);
  CHECK(requester.binds == 1);
  CHECK(requester.sync->FindNode(node_id).id() == node_id);
}

void TestRetrySameRequestAfterOutage() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(true);
  requester.Start(false);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "retry-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      requester.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(requester.sends() == 1);
  auto const saved = requester.probe->last;
  network.ClearQueues();
  network.SetAvailability(kB, kA, EndpointAvailability::Offline);
  Silence(requester, network, kA, 300);
  CHECK(requester.sends() == 1);
  CHECK(network.PendingCount(kB, kA) == 0);

  GoOnline(requester, network, kA);
  requester.sync->Service(g_now);
  CHECK(requester.sends() == 2);
  CHECK(requester.probe->last == saved);
  ShareOfferFrame frame;
  CHECK(DecodeShareRequestFrame(requester.probe->last, frame));
  CHECK(frame.operation_id == op);
  CHECK(frame.target_node_id == node_id);

  World world{&network, {&holder, &requester}};
  Pump(world, [&] {
    return requester.sync->FindNode(node_id).is_valid() &&
           holder.sync->OfferPhase(op) == ShareOfferPhase::Complete;
  });
  ExpectPayload(requester.sync->FindNode(node_id), "retry-seed", kA, 1);
}

void TestAcceptWhileOffline() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(false);
  requester.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "accept-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      requester.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kB, kA));
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::AwaitingDecision);
  CHECK(holder.sends() == 0);

  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  holder.sync->AcceptJoin(op, ShareAccess::ReadWrite);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Accepted);
  CHECK(holder.sends() == 0);
  CHECK(network.PendingCount(kA, kB) == 0);
  CHECK(!requester.sync->FindNode(node_id).is_valid());
  Silence(holder, network, kB, 250);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Accepted);

  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 2);
  ShareDecisionFrame decision;
  CHECK(DecodeShareDecisionFrame(network.PeekNext(kA, kB), decision));
  CHECK(decision.operation_id == op);
  CHECK(decision.accepted);
  CHECK(decision.target_node_id == node_id);
  CHECK(network.DeliverNext(kA, kB));
  NodeStateFrame snapshot;
  CHECK(DecodeNodeStateFrame(network.PeekNext(kA, kB), snapshot));
  CHECK(snapshot.target_node_id == node_id);
  CHECK(!Contains(network.PeekNext(kA, kB), kSecret));
  auto const snapshot_id = snapshot.packet_id;
  CHECK(network.DeliverNext(kA, kB));
  CHECK(requester.binds == 1);
  CHECK(requester.sync->OfferPhase(op) == ShareOfferPhase::Bound);
  AckFrame ack;
  CHECK(DecodeAckFrame(network.PeekNext(kB, kA), ack));
  CHECK(ack.packet_id == snapshot_id);
  CHECK(ack.target_node_id == node_id);
  CHECK(network.DeliverNext(kB, kA));
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Complete);
  ExpectPayload(requester.sync->FindNode(node_id), "accept-seed", kA, 1);
  CHECK(AsRecord(requester.sync->FindNode(node_id))->records.size() == 1);
}

void TestRejectWhileOffline() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(false);
  requester.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "reject-seed", kSecret);
  auto const op =
      requester.sync->RequestJoin(kA, node.id(), ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kB, kA));
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  holder.sync->RejectJoin(op);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Rejected);
  CHECK(holder.sends() == 0);
  Silence(holder, network, kB, 100);
  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 1);
  ShareDecisionFrame decision;
  CHECK(DecodeShareDecisionFrame(holder.probe->last, decision));
  CHECK(decision.operation_id == op);
  CHECK(!decision.accepted);
  CHECK(network.DeliverNext(kA, kB));
  CHECK(requester.sync->OfferPhase(op) == ShareOfferPhase::Rejected);
  CHECK(!requester.sync->FindNode(node.id()).is_valid());
  auto const sends = holder.sends();
  Silence(holder, network, kB, 50);
  CHECK(holder.sends() == sends);
}

void TestSnapshotRetryAfterOutage() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(false);
  requester.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "snap-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      requester.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kB, kA));
  holder.sync->AcceptJoin(op, ShareAccess::ReadWrite);
  CHECK(holder.sends() == 2);
  CHECK(network.DeliverNext(kA, kB));
  auto const saved = network.PeekNext(kA, kB);
  NodeStateFrame snapshot;
  CHECK(DecodeNodeStateFrame(saved, snapshot));
  CHECK(snapshot.target_node_id == node_id);
  auto const packet_id = snapshot.packet_id;
  CHECK(network.DropNext(kA, kB));

  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  Silence(holder, network, kB, 400);
  CHECK(holder.sends() == 2);
  CHECK(network.PendingCount(kA, kB) == 0);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Accepted);

  GoOnline(holder, network, kB);
  auto const before = holder.sends();
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  CHECK(holder.probe->last == saved);
  NodeStateFrame again;
  CHECK(DecodeNodeStateFrame(network.PeekNext(kA, kB), again));
  CHECK(again.packet_id == packet_id);
  CHECK(again.target_node_id == node_id);
  CHECK(network.DeliverNext(kA, kB));
  CHECK(requester.binds == 1);
  CHECK(network.DeliverNext(kB, kA));
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Complete);
  ExpectPayload(requester.sync->FindNode(node_id), "snap-seed", kA, 1);
  CHECK(AsRecord(requester.sync->FindNode(node_id))->records.size() == 1);
}

void TestSnapshotSavedBeforeAck() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(false);
  requester.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "saved-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      requester.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kB, kA));
  holder.sync->AcceptJoin(op, ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kA, kB));
  NodeStateFrame snapshot;
  CHECK(DecodeNodeStateFrame(network.PeekNext(kA, kB), snapshot));
  auto const packet_id = snapshot.packet_id;

  network.SetAvailability(kB, kA, EndpointAvailability::Offline);
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  CHECK(network.DeliverNext(kA, kB));
  CHECK(requester.sync->FindNode(node_id).is_valid());
  CHECK(requester.binds == 1);
  CHECK(requester.sends() == 1);
  CHECK(network.PendingCount(kB, kA) == 0);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Accepted);
  ExpectPayload(requester.sync->FindNode(node_id), "saved-seed", kA, 1);
  Silence(requester, network, kA, 300);
  Silence(holder, network, kB, 0);
  CHECK(requester.sends() == 1);
  CHECK(holder.sync->OfferPhase(op) != ShareOfferPhase::Complete);

  GoOnline(requester, network, kA);
  requester.sync->Service(g_now);
  CHECK(requester.sends() == 2);
  AckFrame ack;
  CHECK(DecodeAckFrame(requester.probe->last, ack));
  CHECK(ack.packet_id == packet_id);
  CHECK(ack.target_node_id == node_id);
  CHECK(network.PendingCount(kB, kA) == 1);
  CHECK(network.DeliverNext(kB, kA));
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Complete);
  CHECK(requester.binds == 1);
  CHECK(AsRecord(requester.sync->FindNode(node_id))->records.size() == 1);
}

void TestUnackedEvent() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(true);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "base-row", kSecret);
  auto const node_id = node.id();
  auto remote = holder.MakeLink(kB);
  auto const op = holder.sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  World world{&network, {&holder, &peer}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op) == ShareOfferPhase::Complete &&
           peer.sync->FindNode(node_id).is_valid();
  });
  auto concrete = AsRecord(holder.sync->FindNode(node_id));
  AddRecord(*concrete, "event-row", kA, 2, 2000);
  auto const before = holder.sends();
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  auto const saved = holder.probe->last;
  EventFrame event;
  CHECK(DecodeEventFrame(saved, event));
  CHECK(event.target_node_id == node_id);
  CHECK(event.identity.origin_uid == kA);
  CHECK(event.identity.origin_sequence == 2);
  CHECK(network.DropNext(kA, kB));

  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  Silence(holder, network, kB, 220);
  CHECK(holder.sends() == before + 1);
  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 2);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 2);
  CHECK(holder.probe->last == saved);
  CHECK(network.DeliverNext(kA, kB));
  auto remote_node = peer.sync->FindNode(node_id);
  ExpectPayload(remote_node, "event-row", kA, 2);
  CHECK(AsRecord(remote_node)->records.size() == 2);
  CHECK(network.DeliverNext(kB, kA));
  CHECK(AsRecord(peer.sync->FindNode(node_id))->records.size() == 2);
}

void TestLostAckWhileOnline() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(true);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "online-base", kSecret);
  auto const node_id = node.id();
  auto remote = holder.MakeLink(kB);
  auto const op = holder.sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  World world{&network, {&holder, &peer}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op) == ShareOfferPhase::Complete;
  });

  network.Disconnect(kA, kB);
  CHECK(holder.transport->Availability(kB) == EndpointAvailability::Online);
  auto concrete = AsRecord(holder.sync->FindNode(node_id));
  AddRecord(*concrete, "dropped-row", kA, 2, 3000);
  auto const before = holder.sends();
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  CHECK(network.PendingCount(kA, kB) == 0);
  auto const saved = holder.probe->last;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  g_now += kShareOfferRetryIntervalUs - 1;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  g_now += 1;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 2);
  CHECK(holder.probe->last == saved);
  CHECK(network.PendingCount(kA, kB) == 0);

  network.Reconnect(kA, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 2);
  g_now += kShareOfferRetryIntervalUs;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 3);
  CHECK(holder.probe->last == saved);
  CHECK(network.PeekNext(kA, kB) == saved);
  CHECK(network.DeliverNext(kA, kB));
  ExpectPayload(peer.sync->FindNode(node_id), "dropped-row", kA, 2);
  CHECK(AsRecord(peer.sync->FindNode(node_id))->records.size() == 2);
  CHECK(network.DropNext(kB, kA));
  auto const mid = holder.sends();
  holder.sync->Service(g_now);
  CHECK(holder.sends() == mid);
  g_now += kShareOfferRetryIntervalUs;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == mid + 1);
  CHECK(holder.probe->last == saved);
  CHECK(network.DeliverNext(kA, kB));
  CHECK(AsRecord(peer.sync->FindNode(node_id))->records.size() == 2);
  CHECK(network.DeliverNext(kB, kA));
  auto const done = holder.sends();
  g_now += 20 * kShareOfferRetryIntervalUs;
  holder.sync->Service(g_now);
  peer.sync->Service(g_now);
  CHECK(holder.sends() == done);
}

void TestUnknownDoesNotBlock() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica requester(network, kB);
  holder.Start(true);
  requester.Start(false);
  CHECK(holder.transport->Availability(kB) == EndpointAvailability::Unknown);
  CHECK(requester.transport->Availability(kA) == EndpointAvailability::Unknown);
  auto node = MakeRecord(holder, "unknown-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      requester.sync->RequestJoin(kA, node_id, ShareAccess::ReadOnly);
  CHECK(requester.sends() == 1);
  World world{&network, {&holder, &requester}};
  Pump(world, [&] {
    return requester.sync->FindNode(node_id).is_valid() &&
           holder.sync->OfferPhase(op) == ShareOfferPhase::Complete;
  });
  ExpectPayload(requester.sync->FindNode(node_id), "unknown-seed", kA, 1);
  CHECK(requester.sync->FindNode(node_id).id() == node_id);
}

void TestIndependentEndpoints() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica bee(network, kB);
  Replica cee(network, kC);
  holder.Start(false);
  bee.Start(true);
  cee.Start(true);
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  network.SetAvailability(kA, kC, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  network.SetAvailability(kC, kA, EndpointAvailability::Online);

  auto node_x = MakeRecord(holder, "payload-x", kSecret);
  auto node_y = MakeRecord(holder, "payload-y", kSecret + "-y");
  auto node_z = MakeRecord(holder, "payload-z", kSecret + "-z");
  auto const id_x = node_x.id();
  auto const id_y = node_y.id();
  auto const id_z = node_z.id();
  auto link_c = holder.MakeLink(kC);
  auto const op_z =
      holder.sync->OfferNode(node_z, link_c, ShareAccess::ReadWrite);
  CHECK(holder.sent_to(kB) == 0);
  CHECK(holder.sent_to(kC) == 1);

  World world{&network, {&holder, &bee, &cee}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op_z) == ShareOfferPhase::Complete &&
           cee.sync->FindNode(id_z).is_valid();
  });
  CHECK(holder.sent_to(kB) == 0);
  CHECK(!bee.sync->FindNode(id_z).is_valid());
  ExpectPayload(cee.sync->FindNode(id_z), "payload-z", kA, 1);

  auto link_b1 = holder.MakeLink(kB);
  auto link_b2 = holder.MakeLink(kB);
  auto const op_x =
      holder.sync->OfferNode(node_x, link_b1, ShareAccess::ReadWrite);
  auto const op_y =
      holder.sync->OfferNode(node_y, link_b2, ShareAccess::ReadWrite);
  CHECK(op_x != op_y);
  CHECK(holder.sent_to(kB) == 0);
  CHECK(holder.sync->OfferPhase(op_x) == ShareOfferPhase::Pending);
  CHECK(holder.sync->OfferPhase(op_y) == ShareOfferPhase::Pending);
  Silence(holder, network, kB, 80);
  CHECK(holder.sent_to(kB) == 0);
  CHECK(cee.sync->FindNode(id_z).is_valid());

  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sent_to(kB) == 2);
  ShareOfferFrame first;
  ShareOfferFrame second;
  CHECK(DecodeShareOfferFrame(network.PeekNext(kA, kB), first));
  CHECK(network.DeliverNext(kA, kB));
  CHECK(DecodeShareOfferFrame(network.PeekNext(kA, kB), second));
  CHECK(first.packet_id != second.packet_id);
  CHECK(first.operation_id != second.operation_id);
  CHECK(first.target_node_id != second.target_node_id);
  CHECK((first.target_node_id == id_x || first.target_node_id == id_y));
  CHECK((second.target_node_id == id_x || second.target_node_id == id_y));

  Pump(world, [&] {
    return holder.sync->OfferPhase(op_x) == ShareOfferPhase::Complete &&
           holder.sync->OfferPhase(op_y) == ShareOfferPhase::Complete &&
           bee.sync->FindNode(id_x).is_valid() &&
           bee.sync->FindNode(id_y).is_valid();
  });
  ExpectPayload(bee.sync->FindNode(id_x), "payload-x", kA, 1);
  ExpectPayload(bee.sync->FindNode(id_y), "payload-y", kA, 1);
  CHECK(!bee.sync->FindNode(id_z).is_valid());
  CHECK(!cee.sync->FindNode(id_x).is_valid());
  CHECK(bee.sync->FindNode(id_x).id() == id_x);
  CHECK(bee.sync->FindNode(id_y).id() == id_y);

  auto share_of = [](SharedNode::ptr node, std::string const& endpoint) {
    for (auto const& share : node->shares) {
      if (!share.link.is_loaded()) {
        share.link.Load();
      }
      if (share.link->EndpointUid() == endpoint) {
        return share.share_id;
      }
    }
    return ae::ObjId{};
  };
  auto const share_x = share_of(holder.sync->FindNode(id_x), kB);
  auto const share_y = share_of(holder.sync->FindNode(id_y), kB);
  CHECK(share_x.is_valid());
  CHECK(share_y.is_valid());
  CHECK(share_x != share_y);
  CHECK(share_of(bee.sync->FindNode(id_x), kB) == share_x);
  CHECK(share_of(bee.sync->FindNode(id_y), kB) == share_y);
}

void TestRepeatedOnlineDoesNotBypassRetry() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "clock-seed", kSecret);
  auto remote = holder.MakeLink(kB);
  auto const op = holder.sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  CHECK(holder.sends() == 1);
  network.ClearQueues();
  auto const wakes = holder.wakes;
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(holder.wakes == wakes);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 1);
  g_now += kShareOfferRetryIntervalUs - 1;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 1);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Pending);
  g_now += 1;
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 2);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 2);
}

std::vector<std::uint8_t> PendingInitial(SharedNode::ptr node,
                                         std::string const& endpoint) {
  for (auto const& share : node->shares) {
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    if (share.link->EndpointUid() != endpoint) {
      continue;
    }
    auto const index = node->FindLinkSyncIndexForShare(share.share_id);
    CHECK(index < node->link_sync_states.size());
    auto state = node->link_sync_states[index];
    if (!state.is_loaded()) {
      state.Load();
    }
    CHECK(!state->pending_initial_packet.empty());
    return state->pending_initial_packet;
  }
  CHECK(false);
  return {};
}

void TestRestartContinuesSavedExchange() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "restart-seed", kSecret);
  auto const node_id = node.id();
  auto const op =
      peer.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kB, kA));
  holder.sync->AcceptJoin(op, ShareAccess::ReadWrite);
  CHECK(network.DeliverNext(kA, kB));
  auto const saved_snapshot = PendingInitial(node, kB);
  CHECK(network.PeekNext(kA, kB) == saved_snapshot);
  NodeStateFrame snapshot;
  CHECK(DecodeNodeStateFrame(saved_snapshot, snapshot));
  CHECK(snapshot.target_node_id == node_id);
  node = {};
  holder.Stop();
  network.ClearQueues();
  holder.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  CHECK(holder.sync->FindNode(node_id).is_valid());
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Accepted);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 0);
  CHECK(network.PendingCount(kA, kB) == 0);
  Silence(holder, network, kB, 150);

  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 1);
  CHECK(holder.probe->last == saved_snapshot);
  CHECK(network.DeliverNext(kA, kB));
  CHECK(peer.binds == 1);
  CHECK(network.DeliverNext(kB, kA));
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Complete);
  ExpectPayload(peer.sync->FindNode(node_id), "restart-seed", kA, 1);

  auto concrete = AsRecord(holder.sync->FindNode(node_id));
  AddRecord(*concrete, "pending-event", kA, 2, 5000);
  holder.sync->Service(g_now);
  auto const saved_event = holder.probe->last;
  EventFrame event;
  CHECK(DecodeEventFrame(saved_event, event));
  CHECK(event.identity.origin_uid == kA);
  CHECK(event.identity.origin_sequence == 2);
  CHECK(event.target_node_id == node_id);
  concrete = {};
  holder.Stop();
  network.ClearQueues();
  holder.Start(false);
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 0);
  CHECK(network.PendingCount(kA, kB) == 0);
  GoOnline(holder, network, kB);
  holder.sync->Service(g_now);
  CHECK(holder.sends() == 1);
  CHECK(holder.probe->last == saved_event);
  CHECK(network.DeliverNext(kA, kB));
  ExpectPayload(peer.sync->FindNode(node_id), "pending-event", kA, 2);
  CHECK(AsRecord(peer.sync->FindNode(node_id))->records.size() == 2);
  CHECK(network.DeliverNext(kB, kA));

  concrete = AsRecord(holder.sync->FindNode(node_id));
  AddRecord(*concrete, "after-restart", kA, 3, 6000);
  auto const before = holder.sends();
  holder.sync->Service(g_now);
  CHECK(holder.sends() == before + 1);
  EventFrame fresh;
  CHECK(DecodeEventFrame(holder.probe->last, fresh));
  CHECK(fresh.identity.origin_sequence == 3);
  CHECK(fresh.packet_id != event.packet_id);
  CHECK(network.DeliverNext(kA, kB));
  ExpectPayload(peer.sync->FindNode(node_id), "after-restart", kA, 3);
  CHECK(AsRecord(peer.sync->FindNode(node_id))->records.size() == 3);
  CHECK(peer.binds == 1);
}

void TestNoExtraTraffic() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(true);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  auto node = MakeRecord(holder, "quiet-seed", kSecret);
  auto const node_id = node.id();
  auto remote = holder.MakeLink(kB);
  auto const op = holder.sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  World world{&network, {&holder, &peer}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op) == ShareOfferPhase::Complete &&
           peer.sync->OfferPhase(op) == ShareOfferPhase::Bound;
  });
  for (int step = 0; step < 8; ++step) {
    Deliver(world);
  }
  auto const records_before =
      AsRecord(holder.sync->FindNode(node_id))->records.size();
  auto const peer_records =
      AsRecord(peer.sync->FindNode(node_id))->records.size();
  auto const offers_before = holder.sync->OfferStatuses().size();
  auto const holder_sends = holder.sends();
  auto const peer_sends = peer.sends();
  CHECK(network.PendingCount(kA, kB) == 0);
  CHECK(network.PendingCount(kB, kA) == 0);

  g_now += 500 * kShareOfferRetryIntervalUs;
  holder.sync->Service(g_now);
  peer.sync->Service(g_now);
  CHECK(holder.sends() == holder_sends);
  CHECK(peer.sends() == peer_sends);
  CHECK(network.PendingCount(kA, kB) == 0);
  CHECK(network.PendingCount(kB, kA) == 0);
  CHECK(holder.sync->OfferPhase(op) == ShareOfferPhase::Complete);
  CHECK(holder.sync->OfferStatuses().size() == offers_before);
  CHECK(AsRecord(holder.sync->FindNode(node_id))->records.size() ==
        records_before);
  CHECK(AsRecord(peer.sync->FindNode(node_id))->records.size() == peer_records);

  auto const wakes = holder.wakes;
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);
  CHECK(holder.wakes == wakes);
  holder.sync->Service(g_now);
  peer.sync->Service(g_now);
  CHECK(holder.sends() == holder_sends);
  CHECK(peer.sends() == peer_sends);
  CHECK(holder.sync->OfferStatuses().size() == offers_before);
}

void TestSameNetworkRestartDropsAvailability() {
  g_now = 0;
  MemoryNetwork network;
  Replica holder(network, kA);
  Replica peer(network, kB);
  holder.Start(false);
  peer.Start(true);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  network.SetAvailability(kB, kA, EndpointAvailability::Online);

  auto node = MakeRecord(holder, "keep-seed", kSecret);
  auto const node_id = node.id();
  auto remote = holder.MakeLink(kB);
  auto const op =
      holder.sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  World world{&network, {&holder, &peer}};
  Pump(world, [&] {
    return holder.sync->OfferPhase(op) == ShareOfferPhase::Complete &&
           peer.sync->FindNode(node_id).is_valid() &&
           peer.sync->OfferPhase(op) == ShareOfferPhase::Bound;
  });
  ExpectPayload(peer.sync->FindNode(node_id), "keep-seed", kA, 1);
  CHECK(holder.transport->Availability(kB) == EndpointAvailability::Online);
  CHECK(peer.transport->Availability(kA) == EndpointAvailability::Online);

  network.Disconnect(kB, kA);
  node = {};
  remote = {};
  holder.Stop();
  holder.Start(false);
  CHECK(holder.transport->Availability(kB) == EndpointAvailability::Unknown);
  CHECK(network.Availability(kA, kB) == EndpointAvailability::Unknown);
  CHECK(peer.transport->Availability(kA) == EndpointAvailability::Online);
  CHECK(!network.IsConnected(kB, kA));
  CHECK(network.IsConnected(kA, kB));

  network.Reconnect(kB, kA);
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(holder.transport->Availability(kB) == EndpointAvailability::Online);
  CHECK(holder.sync->FindNode(node_id).is_valid());

  auto concrete = AsRecord(holder.sync->FindNode(node_id));
  AddRecord(*concrete, "after-new-observation", kA, 2, 7000);
  concrete = {};
  Pump(world, [&] {
    auto peer_node = peer.sync->FindNode(node_id);
    if (!peer_node.is_valid()) {
      return false;
    }
    for (auto const& text : AsRecord(peer_node)->records) {
      if (text == "after-new-observation") {
        return true;
      }
    }
    return false;
  });
  ExpectPayload(peer.sync->FindNode(node_id), "after-new-observation", kA, 2);
  ExpectPayload(holder.sync->FindNode(node_id), "keep-seed", kA, 1);
}

void TestAvailabilityReachesRuntimeOnlyWhenDrained() {
  g_now = 0;
  MemoryNetwork network;
  MemoryTransport raw_a(network, kA);
  MemoryTransport raw_b(network, kB);
  QueuedTransport queued(raw_a);
  ae::RamDomainStorage storage;
  auto domain = std::make_unique<ae::Domain>(storage);
  auto sync = std::make_unique<SharedSyncRuntime>(*domain, storage, queued);
  int wakes = 0;
  sync->SetAvailabilityWake([&] { ++wakes; });

  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  CHECK(queued.queued_availability() == 1);
  CHECK(queued.Availability(kB) == EndpointAvailability::Unknown);
  CHECK(wakes == 0);
  CHECK(queued.availability_deliveries == 0);

  queued.Drain();
  CHECK(queued.queued_availability() == 0);
  CHECK(queued.Availability(kB) == EndpointAvailability::Offline);
  CHECK(wakes == 1);
  CHECK(queued.availability_deliveries == 1);

  auto node = AvailRecordNode::ptr::Create(ae::CreateWith{*domain});
  InitializeRuntimeNode(*node);
  auto self = MemoryLink::ptr::Create(
      ae::CreateWith{*domain}.with_id(ae::ObjId::GenerateUnique()));
  self->endpoint_uid = kA;
  self->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*self);
  self.Save();
  node->AddShare(self, ShareAccess::ReadWrite);
  node.Save();
  sync->RegisterNode(node);
  auto remote = MemoryLink::ptr::Create(
      ae::CreateWith{*domain}.with_id(ae::ObjId::GenerateUnique()));
  remote->endpoint_uid = kB;
  remote->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*remote);
  remote.Save();
  auto const op = sync->OfferNode(node, remote, ShareAccess::ReadWrite);
  sync->Service(0);
  g_now = 4 * kShareOfferRetryIntervalUs;
  sync->Service(g_now);
  CHECK(sync->OfferPhase(op) == ShareOfferPhase::Pending);
  CHECK(queued.sends == 0);
  CHECK(network.PendingCount(kA, kB) == 0);

  auto const wakes_before_online = wakes;
  auto const deliveries_before_online = queued.availability_deliveries;
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(queued.queued_availability() == 1);
  CHECK(queued.Availability(kB) == EndpointAvailability::Offline);
  CHECK(wakes == wakes_before_online);
  sync->Service(g_now);
  CHECK(queued.sends == 0);
  CHECK(sync->OfferPhase(op) == ShareOfferPhase::Pending);

  queued.Drain();
  CHECK(queued.Availability(kB) == EndpointAvailability::Online);
  CHECK(wakes == wakes_before_online + 1);
  CHECK(queued.availability_deliveries == deliveries_before_online + 1);
  sync->Service(g_now);
  CHECK(queued.sends == 1);
  CHECK(network.PendingCount(kA, kB) == 1);

  raw_b.Send(kA, std::vector<std::uint8_t>{9, 9, 9});
  CHECK(network.DeliverNext(kB, kA));
  CHECK(queued.queued_receives() == 1);
  CHECK(queued.receive_deliveries == 0);

  auto const deliveries = queued.availability_deliveries;
  auto const receives = queued.receive_deliveries;
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  CHECK(queued.queued_availability() == 1);
  sync.reset();
  CHECK(queued.queued_availability() == 0);
  CHECK(queued.queued_receives() == 0);
  queued.Drain();
  CHECK(queued.availability_deliveries == deliveries);
  CHECK(queued.receive_deliveries == receives);
  CHECK(wakes == wakes_before_online + 1);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestTransportAvailability();
  apptraverse::test::TestInitialOfflineOffer();
  apptraverse::test::TestInitialOfflineRequest();
  apptraverse::test::TestRetrySameRequestAfterOutage();
  apptraverse::test::TestAcceptWhileOffline();
  apptraverse::test::TestRejectWhileOffline();
  apptraverse::test::TestSnapshotRetryAfterOutage();
  apptraverse::test::TestSnapshotSavedBeforeAck();
  apptraverse::test::TestUnackedEvent();
  apptraverse::test::TestLostAckWhileOnline();
  apptraverse::test::TestUnknownDoesNotBlock();
  apptraverse::test::TestIndependentEndpoints();
  apptraverse::test::TestRepeatedOnlineDoesNotBypassRetry();
  apptraverse::test::TestRestartContinuesSavedExchange();
  apptraverse::test::TestNoExtraTraffic();
  apptraverse::test::TestSameNetworkRestartDropsAvailability();
  apptraverse::test::TestAvailabilityReachesRuntimeOnlyWhenDrained();
  return 0;
}
