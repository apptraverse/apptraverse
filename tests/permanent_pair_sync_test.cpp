#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
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
std::string const kSecret = "PAIR_LOCAL_SECRET_do_not_ship";
ae::ObjId const kPairBindingId{0x50414952};  // "PAIR"

// One SharedNode = one permanent A↔B dialog. After Bound there are exactly two
// ReadWrite shares; this profile never removes or extends that topology.

class PairSecret : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PairSecret", PairSecret, ae::Obj,
                           0)

 protected:
  PairSecret() = default;

 public:
  explicit PairSecret(ae::ObjProp prop) : Obj{prop} {}
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

class AddPairRecordEvent;
class PairDialogNode
    : public apptraverse::NodeFor<PairDialogNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PairDialogNode", PairDialogNode,
                           SharedNode, 0)

 protected:
  PairDialogNode() = default;

 public:
  explicit PairDialogNode(ae::ObjProp prop) : NodeFor{prop} {}
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
  apptraverse::LocalPtr<PairSecret> local_secret;

  void Apply(AddPairRecordEvent const& event);
};

class AddPairRecordEvent
    : public apptraverse::EventFor<PairDialogNode, AddPairRecordEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::AddPairRecordEvent",
                           AddPairRecordEvent, Event, 0)

 protected:
  AddPairRecordEvent() = default;

 public:
  explicit AddPairRecordEvent(ae::ObjProp prop) : EventFor{prop} {}
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

void PairDialogNode::Apply(AddPairRecordEvent const& event) {
  records.push_back(event.text);
  NoteMaterializedChange();
}

class PairBinding : public apptraverse::NodeFor<PairBinding> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::PairBinding", PairBinding, Node,
                           0)

 protected:
  PairBinding() = default;

 public:
  explicit PairBinding(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT()
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, node_ids);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, node_ids);
  }
  std::vector<std::uint32_t> node_ids;
};

APPTRAVERSE_REGISTER(PairSecret);
APPTRAVERSE_REGISTER(PairDialogNode);
APPTRAVERSE_REGISTER(AddPairRecordEvent);
APPTRAVERSE_REGISTER(PairBinding);

class SendCounter final : public IByteTransport {
 public:
  SendCounter(MemoryTransport& inner, std::uint64_t& sends)
      : inner_{inner}, sends_{sends} {}

  std::string const& local_endpoint_uid() const override {
    return inner_.local_endpoint_uid();
  }
  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override {
    ++sends_;
    ++by_dest[destination_endpoint];
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

  std::map<std::string, std::uint64_t> by_dest;

 private:
  MemoryTransport& inner_;
  std::uint64_t& sends_;
};

MemoryLink::ptr MakeLink(ae::Domain& domain, std::string endpoint);

struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint)
      : storage{}, network_{network}, endpoint_{std::move(endpoint)} {}

  void Start() {
    binds = 0;
    domain = std::make_unique<ae::Domain>(storage);
    transport = std::make_unique<MemoryTransport>(network_, endpoint_);
    counter = std::make_unique<SendCounter>(*transport, sends);
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, *counter);
    sync->AllowStandaloneEventClass(AddPairRecordEvent::kClassId);
    sync->SetInitialNodeImportedCallback(
        [this](std::string const&, SharedNode::ptr node) {
          ++binds;
          PairBinding::ptr binding;
          if (storage.Enumerate(kPairBindingId).empty()) {
            binding = PairBinding::ptr::Create(
                ae::CreateWith{*domain}.with_id(kPairBindingId));
            InitializeRuntimeNode(*binding);
          } else {
            binding = PairBinding::ptr::Declare(
                ae::CreateWith{*domain}.with_id(kPairBindingId));
            binding.Load();
            CHECK(binding.is_loaded());
          }
          auto const id = node.id().id();
          bool found = false;
          for (auto existing : binding->node_ids) {
            if (existing == id) {
              found = true;
            }
          }
          if (!found) {
            binding->node_ids.push_back(id);
          }
          binding.Save();
          return true;
        });
  }

  void Stop() {
    sync.reset();
    counter.reset();
    transport.reset();
    domain.reset();
  }

  std::string const& endpoint() const { return endpoint_; }
  std::uint64_t sent_to(std::string const& dest) const {
    auto const it = counter->by_dest.find(dest);
    return it == counter->by_dest.end() ? 0 : it->second;
  }

  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<SendCounter> counter;
  std::unique_ptr<SharedSyncRuntime> sync;
  std::uint64_t sends{0};
  std::uint64_t clock{0};
  int binds{0};

 private:
  MemoryNetwork& network_;
  std::string endpoint_;
};

MemoryLink::ptr MakeLink(ae::Domain& domain, std::string endpoint) {
  auto link = MemoryLink::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId::GenerateUnique()));
  link->endpoint_uid = std::move(endpoint);
  link->heartbeat_interval_ms = 1000;
  InitializeRuntimeNode(*link);
  link.Save();
  return link;
}

void SaveSync(SharedNode::ptr node) {
  node.Save();
  for (auto& entry : node->link_sync_states) {
    entry.Save();
  }
}

PairDialogNode::ptr MakeDialog(Replica& replica) {
  auto node = PairDialogNode::ptr::Create(ae::CreateWith{*replica.domain});
  InitializeRuntimeNode(*node);
  auto hidden = PairSecret::ptr::Create(ae::CreateWith{*replica.domain});
  hidden->mark = kSecret;
  hidden.Save();
  node->local_secret = hidden;
  auto self = MakeLink(*replica.domain, replica.endpoint());
  node->InstallLocalShare(self, ShareAccess::ReadWrite);
  SaveSync(node);
  replica.sync->RegisterNode(node);
  return node;
}

void AddRecord(PairDialogNode& node, std::string text, std::string origin,
               std::uint64_t sequence, std::uint64_t timestamp_us) {
  auto event = AddPairRecordEvent::ptr::Create(ae::CreateWith{*node.domain});
  event->text = std::move(text);
  node.CommitShared(std::move(event),
                    SharedEventId{.origin_uid = std::move(origin),
                                  .origin_sequence = sequence},
                    SharedEventOrder{.timestamp_us = timestamp_us});
  PairDialogNode::ptr::MakeFromThis(&node).Save();
}

std::uint64_t NextLocalStamp(PairDialogNode const& node) {
  std::uint64_t stamp = 1;
  for (auto const& record : node.journal) {
    if (record.order.timestamp_us >= stamp) {
      stamp = record.order.timestamp_us + 1;
    }
  }
  return stamp;
}

void AddRecordLocal(PairDialogNode& node, std::string text, std::string origin,
                    std::uint64_t sequence) {
  AddRecord(node, std::move(text), std::move(origin), sequence,
            NextLocalStamp(node));
}

PairDialogNode::ptr AsDialog(SharedNode::ptr node) {
  PairDialogNode::ptr concrete = node;
  concrete.Load();
  CHECK(concrete.is_loaded());
  return concrete;
}

struct Observed {
  SharedEventId id;
  std::uint64_t timestamp_us{0};
  std::string text;
};

// Permanent-pair Observe oracle: (timestamp_us, origin_uid, origin_sequence).
// SharedEventOrderLess remains timestamp-only; equal timestamps are an open
// journal-order case and must not be hidden by unique stamps in this profile.
std::vector<Observed> Observe(PairDialogNode const& node) {
  std::vector<Observed> out;
  for (auto const& record : node.journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    auto event = record.event;
    if (!event.is_loaded()) {
      event.Load();
    }
    if (event->GetClassId() != AddPairRecordEvent::kClassId) {
      continue;
    }
    AddPairRecordEvent::ptr concrete = event;
    CHECK(concrete.is_loaded());
    out.push_back(Observed{.id = record.identity,
                           .timestamp_us = record.order.timestamp_us,
                           .text = concrete->text});
  }
  std::sort(out.begin(), out.end(), [](Observed const& a, Observed const& b) {
    if (a.timestamp_us != b.timestamp_us) {
      return a.timestamp_us < b.timestamp_us;
    }
    if (a.id.origin_uid != b.id.origin_uid) {
      return a.id.origin_uid < b.id.origin_uid;
    }
    return a.id.origin_sequence < b.id.origin_sequence;
  });
  return out;
}

bool HasText(SharedNode::ptr node, std::string const& text,
             std::string const& origin, std::uint64_t sequence) {
  for (auto const& seen : Observe(*AsDialog(node))) {
    if (seen.text == text && seen.id.origin_uid == origin &&
        seen.id.origin_sequence == sequence) {
      return true;
    }
  }
  return false;
}

struct ShareView {
  ae::ObjId share_id;
  ae::ObjId link_id;
  std::string endpoint;
  ShareAccess access{ShareAccess::ReadWrite};
};

std::vector<ShareView> SharesOf(SharedNode::ptr node) {
  std::vector<ShareView> out;
  for (auto const& share : node->shares) {
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    CHECK(share.link.is_loaded());
    out.push_back(ShareView{.share_id = share.share_id,
                            .link_id = share.link.id(),
                            .endpoint = share.link->EndpointUid(),
                            .access = share.GetAccess()});
  }
  std::sort(out.begin(), out.end(), [](ShareView const& a, ShareView const& b) {
    return a.endpoint < b.endpoint;
  });
  return out;
}

void ExpectPairTopology(SharedNode::ptr left, SharedNode::ptr right) {
  CHECK(left.is_valid());
  CHECK(right.is_valid());
  CHECK(left.id() == right.id());
  auto const a = SharesOf(left);
  auto const b = SharesOf(right);
  CHECK(a.size() == 2);
  CHECK(b.size() == 2);
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].share_id == b[i].share_id);
    CHECK(a[i].link_id == b[i].link_id);
    CHECK(a[i].endpoint == b[i].endpoint);
    CHECK(a[i].access == ShareAccess::ReadWrite);
    CHECK(b[i].access == ShareAccess::ReadWrite);
  }
}

void ExpectSameObserve(SharedNode::ptr left, SharedNode::ptr right) {
  auto const a = Observe(*AsDialog(left));
  auto const b = Observe(*AsDialog(right));
  CHECK(a.size() == b.size());
  CHECK(AsDialog(left)->records.size() == a.size());
  CHECK(AsDialog(right)->records.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(a[i].id == b[i].id);
    CHECK(a[i].timestamp_us == b[i].timestamp_us);
    CHECK(a[i].text == b[i].text);
  }
}

bool Contains(std::vector<std::uint8_t> const& bytes, std::string const& needle) {
  if (needle.empty() || bytes.size() < needle.size()) {
    return false;
  }
  std::string_view const view(reinterpret_cast<char const*>(bytes.data()),
                              bytes.size());
  return view.find(needle) != std::string_view::npos;
}

struct World {
  MemoryNetwork* network{nullptr};
  std::vector<Replica*> replicas;
};

bool DeliverRound(World& world) {
  bool any = false;
  for (auto* from : world.replicas) {
    for (auto* to : world.replicas) {
      if (from == to) {
        continue;
      }
      auto const& bytes =
          world.network->PeekNext(from->endpoint(), to->endpoint());
      if (Contains(bytes, kSecret)) {
        std::cerr << "secret leaked on " << from->endpoint() << " -> "
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

bool Queued(World const& world) {
  for (auto* from : world.replicas) {
    for (auto* to : world.replicas) {
      if (from != to &&
          world.network->PendingCount(from->endpoint(), to->endpoint()) != 0) {
        return true;
      }
    }
  }
  return false;
}

std::string Diagnose(World const& world) {
  std::string out;
  for (auto* replica : world.replicas) {
    out += replica->endpoint();
    out += " nodes=";
    out += std::to_string(replica->sync->RegisteredNodeIds().size());
    out += '\n';
  }
  return out;
}

void Pump(World world, auto&& done, int max_steps) {
  for (int step = 0; step < max_steps; ++step) {
    if (done()) {
      return;
    }
    DeliverRound(world);
    for (auto* replica : world.replicas) {
      replica->clock += kShareOfferRetryIntervalUs / 4 + 1;
      replica->sync->Service(replica->clock);
    }
    if (done()) {
      return;
    }
  }
  std::cerr << "pump stopped\n" << Diagnose(world);
  CHECK(false);
}

void Drain(World& world, int max_rounds = 64) {
  for (int i = 0; i < max_rounds; ++i) {
    if (!DeliverRound(world)) {
      return;
    }
  }
}

void Restart(Replica& replica) {
  replica.Stop();
  replica.Start();
}

void Advance(Replica& replica, std::uint64_t delta_us) {
  replica.clock += delta_us;
  replica.sync->Service(replica.clock);
}

void ServiceRetry(Replica& replica, MemoryNetwork& network,
                  std::string const& to) {
  auto const before = network.PendingCount(replica.endpoint(), to);
  replica.sync->Service(replica.clock);
  if (network.PendingCount(replica.endpoint(), to) == before) {
    replica.clock += kShareOfferRetryIntervalUs;
    replica.sync->Service(replica.clock);
  }
}

struct Pair {
  MemoryNetwork network;
  Replica a;
  Replica b;
  Pair() : a(network, kA), b(network, kB) {
    a.Start();
    b.Start();
  }
  World world() { return World{.network = &network, .replicas = {&a, &b}}; }
};

struct Trio {
  MemoryNetwork network;
  Replica a;
  Replica b;
  Replica c;
  Trio() : a(network, kA), b(network, kB), c(network, kC) {
    a.Start();
    b.Start();
    c.Start();
  }
  World world() {
    return World{.network = &network, .replicas = {&a, &b, &c}};
  }
};

LinkSyncState::ptr SyncStateForPeer(SharedNode::ptr node,
                                    std::string const& peer_endpoint) {
  CHECK(node.is_valid());
  for (auto const& share : node->shares) {
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    CHECK(share.link.is_loaded());
    if (share.link->EndpointUid() != peer_endpoint) {
      continue;
    }
    auto const idx = node->FindLinkSyncIndexForShare(share.share_id);
    CHECK(idx < node->link_sync_states.size());
    auto state = node->link_sync_states[idx];
    if (!state.is_loaded()) {
      state.Load();
    }
    CHECK(state.is_loaded());
    return state;
  }
  CHECK(false);
  return {};
}

ae::ObjId PeerShareId(SharedNode::ptr node, std::string const& peer_endpoint) {
  CHECK(node.is_valid());
  for (auto const& share : node->shares) {
    if (!share.link.is_loaded()) {
      share.link.Load();
    }
    CHECK(share.link.is_loaded());
    if (share.link->EndpointUid() == peer_endpoint) {
      return share.share_id;
    }
  }
  return {};
}

bool PairJoined(Pair& pair, ae::ObjId node_id) {
  auto a_node = pair.a.sync->FindNode(node_id);
  auto b_node = pair.b.sync->FindNode(node_id);
  if (!a_node.is_valid() || !b_node.is_valid()) {
    return false;
  }
  if (a_node->shares.size() != 2 || b_node->shares.size() != 2) {
    return false;
  }
  auto state = SyncStateForPeer(a_node, kB);
  return state.is_valid() &&
         state->GetInitialSyncPhase() == InitialSyncPhase::Complete;
}

// Chat formation: holder InstallLocalShare (self already + peer), peer
// ExpectInitial, SyncInitialState. No OfferNode.
void FormPair(Replica& holder, Replica& peer, PairDialogNode::ptr node) {
  auto const node_id = node.id();
  if (node->shares.size() == 2 && peer.sync->FindNode(node_id).is_valid()) {
    auto state = SyncStateForPeer(node, peer.endpoint());
    if (state.is_valid() &&
        state->GetInitialSyncPhase() == InitialSyncPhase::Complete) {
      return;
    }
  }
  if (!PeerShareId(node, peer.endpoint()).is_valid()) {
    auto remote = MakeLink(*holder.domain, peer.endpoint());
    node->InstallLocalShare(remote, ShareAccess::ReadWrite);
    SaveSync(node);
  }
  CHECK(node->shares.size() == 2);
  peer.sync->ExpectInitialNodeFromEndpoint(holder.endpoint(),
                                           PairDialogNode::kClassId, node_id);
  auto const share_id = PeerShareId(node, peer.endpoint());
  CHECK(share_id.is_valid());
  holder.sync->SyncInitialState(node_id, share_id);
}

void FormPair(Pair& pair, PairDialogNode::ptr node) {
  FormPair(pair.a, pair.b, node);
}

void FinishPair(Pair& pair, ae::ObjId node_id) {
  Pump(pair.world(), [&] { return PairJoined(pair, node_id); }, 80);
}

void Settle(World& world, int max_steps = 200) {
  for (int step = 0; step < max_steps; ++step) {
    auto const before = [&] {
      std::uint64_t total = 0;
      for (auto* r : world.replicas) {
        total += r->sends;
      }
      return total;
    }();
    Drain(world);
    for (auto* replica : world.replicas) {
      Advance(*replica, kShareOfferRetryIntervalUs + 1);
    }
    auto const after = [&] {
      std::uint64_t total = 0;
      for (auto* r : world.replicas) {
        total += r->sends;
      }
      return total;
    }();
    if (!Queued(world) && after == before) {
      return;
    }
  }
  std::cerr << "settle failed\n" << Diagnose(world);
  CHECK(false);
}

PairBinding::ptr LoadBinding(Replica& replica) {
  CHECK(!replica.storage.Enumerate(kPairBindingId).empty());
  auto binding = PairBinding::ptr::Declare(
      ae::CreateWith{*replica.domain}.with_id(kPairBindingId));
  binding.Load();
  CHECK(binding.is_loaded());
  return binding;
}

// Re-open a saved dialog: restore runtime registration from storage.
// Binding is required on the importer; optional on the holder.
void ReopenDialog(Replica& replica, ae::ObjId node_id) {
  if (replica.sync->FindNode(node_id).is_valid()) {
    return;
  }
  CHECK(!replica.storage.Enumerate(node_id).empty());
  auto node =
      PairDialogNode::ptr::Declare(ae::CreateWith{*replica.domain}.with_id(node_id));
  node.Load();
  CHECK(node.is_loaded());
  replica.sync->RegisterNode(node);
  if (replica.storage.Enumerate(kPairBindingId).empty()) {
    return;
  }
  auto binding = LoadBinding(replica);
  bool found = false;
  for (auto id : binding->node_ids) {
    if (id == node_id.id()) {
      found = true;
    }
  }
  CHECK(found);
}

void Online(MemoryNetwork& network, std::string const& from,
            std::string const& to) {
  network.Reconnect(from, to);
  network.SetAvailability(from, to, EndpointAvailability::Online);
}

void Offline(MemoryNetwork& network, std::string const& from,
             std::string const& to) {
  network.SetAvailability(from, to, EndpointAvailability::Offline);
}



// Count only real availability transitions (value actually changes).
bool SetAvailabilityTracked(MemoryNetwork& network, std::string const& from,
                            std::string const& to,
                            EndpointAvailability next,
                            int& to_offline, int& to_online) {
  auto const prev = network.Availability(from, to);
  if (prev == next) {
    return false;
  }
  network.SetAvailability(from, to, next);
  if (next == EndpointAvailability::Offline) {
    ++to_offline;
  } else if (next == EndpointAvailability::Online) {
    ++to_online;
  }
  return true;
}


// ---------------------------------------------------------------------------
// Contract / basic scenarios
// ---------------------------------------------------------------------------

void TestFormationAndBasicExchange() {
  Pair pair;
  // B must not be pre-seeded with X.
  CHECK(pair.b.sync->RegisteredNodeIds().empty());
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  CHECK(pair.network.PendingCount(kA, kB) == 1);
  NodeStateFrame first;
  CHECK(DecodeNodeStateFrame(pair.network.PeekNext(kA, kB), first));
  FinishPair(pair, node_id);
  ExpectPairTopology(pair.a.sync->FindNode(node_id),
                     pair.b.sync->FindNode(node_id));
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
  CHECK(pair.b.binds == 1);
  auto binding = LoadBinding(pair.b);
  CHECK(binding->node_ids.size() == 1);
  CHECK(binding->node_ids[0] == node_id.id());

  auto live_a = AsDialog(pair.a.sync->FindNode(node_id));
  auto live_b = AsDialog(pair.b.sync->FindNode(node_id));
  AddRecord(*live_a, "A1", kA, 2, 200);
  AddRecord(*live_b, "B1", kB, 1, 300);
  AddRecord(*live_a, "A2", kA, 3, 400);
  AddRecord(*live_b, "B2", kB, 2, 500);
  auto world = pair.world();
  Pump(world,
       [&] {
         return Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size() == 5 &&
                Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() == 5;
       },
       200);
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
  Settle(world);

  // Already paired: third peer InstallLocalShare refused; shares stay 2.
  CHECK(pair.a.sync->FindNode(node_id)->shares.size() == 2);
  AsDialog(pair.a.sync->FindNode(node_id))
      ->InstallLocalShare(MakeLink(*pair.a.domain, kC), ShareAccess::ReadWrite);
  CHECK(pair.a.sync->FindNode(node_id)->shares.size() == 2);
}

void TestLocalAcceptWhileOffline() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 10);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);
  Offline(pair.network, kA, kB);
  Offline(pair.network, kB, kA);
  auto live_a = AsDialog(pair.a.sync->FindNode(node_id));
  AddRecord(*live_a, "offline-local", kA, 2, 20);
  CHECK(HasText(pair.a.sync->FindNode(node_id), "offline-local", kA, 2));
  CHECK(!HasText(pair.b.sync->FindNode(node_id), "offline-local", kA, 2));
  auto const sends_before = pair.a.sends;
  Advance(pair.a, kShareOfferRetryIntervalUs);
  CHECK(pair.a.sends == sends_before);
  CHECK(pair.network.PendingCount(kA, kB) == 0);
  Online(pair.network, kA, kB);
  Online(pair.network, kB, kA);
  auto world = pair.world();
  Pump(world,
       [&] {
         return HasText(pair.b.sync->FindNode(node_id), "offline-local", kA, 2);
       },
       80);
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
}

void TestLossesDuplicatesReorderDuringSync() {
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    CHECK(pair.network.PendingCount(kA, kB) == 1);
    NodeStateFrame first;
    CHECK(DecodeNodeStateFrame(pair.network.PeekNext(kA, kB), first));
    CHECK(pair.network.DropNext(kA, kB));
    ServiceRetry(pair.a, pair.network, kB);
    FinishPair(pair, node_id);
  }
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.DropNext(kB, kA));
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.PendingCount(kB, kA) == 1);
    FinishPair(pair, node_id);
  }
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    CHECK(pair.network.PendingCount(kA, kB) == 1);
    NodeStateFrame snapshot;
    CHECK(DecodeNodeStateFrame(pair.network.PeekNext(kA, kB), snapshot));
    auto const packet_id = snapshot.packet_id;
    CHECK(pair.network.DropNext(kA, kB));
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.PendingCount(kA, kB) >= 1);
    NodeStateFrame again;
    CHECK(DecodeNodeStateFrame(pair.network.PeekNext(kA, kB), again));
    CHECK(again.packet_id == packet_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "during-initial", kA, 2,
              150);
    FinishPair(pair, node_id);
    auto world = pair.world();
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "during-initial", kA,
                          2);
         },
         80);
    ExpectSameObserve(pair.a.sync->FindNode(node_id),
                      pair.b.sync->FindNode(node_id));
  }
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    FinishPair(pair, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "evt", kA, 2, 200);
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.DuplicateNext(kA, kB));
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() == 2);
    CHECK(pair.network.DropNext(kB, kA));
    ServiceRetry(pair.a, pair.network, kB);
    auto world = pair.world();
    Pump(world,
         [&] {
           return !Queued(world) &&
                  Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size() ==
                      2 &&
                  Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() ==
                      2;
         },
         80);
    ExpectSameObserve(pair.a.sync->FindNode(node_id),
                      pair.b.sync->FindNode(node_id));
  }
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    FinishPair(pair, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "first", kA, 2, 200);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "second", kA, 3, 300);
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.PendingCount(kA, kB) >= 1);
    CHECK(pair.network.DuplicateNext(kA, kB));
    CHECK(pair.network.DeferNext(kA, kB));
    auto world = pair.world();
    Pump(world,
         [&] {
           return Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() ==
                  3;
         },
         80);
    ExpectSameObserve(pair.a.sync->FindNode(node_id),
                      pair.b.sync->FindNode(node_id));
  }
}

void TestWrongSourceDoesNotAck() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);

  AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "pending", kA, 2, 200);
  ServiceRetry(pair.a, pair.network, kB);
  CHECK(pair.network.PendingCount(kA, kB) >= 1);
  EventFrame frame;
  CHECK(DecodeEventFrame(pair.network.PeekNext(kA, kB), frame));
  auto const packet_id = frame.packet_id;
  auto const identity = frame.identity;
  // Remove the original packet without delivering it to B.
  CHECK(pair.network.DropNext(kA, kB));
  CHECK(pair.network.PendingCount(kA, kB) == 0);

  auto state = SyncStateForPeer(pair.a.sync->FindNode(node_id), kB);
  CHECK(state->HasPendingEvent());
  CHECK(state->pending_event_packet_id == packet_id);
  CHECK(!state->HasDelivered(identity));

  struct Eve {
    static void OnBytes(void*, std::string const&,
                        std::vector<std::uint8_t> const&) {}
  };
  MemoryTransport eve{pair.network, "endpoint-eve"};
  eve.BindReceive(nullptr, &Eve::OnBytes);
  AckFrame forged{.packet_id = packet_id,
                  .target_node_id = frame.target_node_id,
                  .destination_share_id = frame.destination_share_id};
  eve.Send(kA, EncodeAckFrame(forged));
  CHECK(pair.network.DeliverNext("endpoint-eve", kA));

  // Wrong source must not mark Delivered or clear the pending wait.
  state = SyncStateForPeer(pair.a.sync->FindNode(node_id), kB);
  CHECK(state->HasPendingEvent());
  CHECK(state->pending_event_packet_id == packet_id);
  CHECK(!state->HasDelivered(identity));

  // Advance to retry: same packet_id must be re-queued.
  auto const sends_before = pair.a.sends;
  ServiceRetry(pair.a, pair.network, kB);
  CHECK(pair.a.sends > sends_before);
  CHECK(pair.network.PendingCount(kA, kB) >= 1);
  EventFrame retried;
  CHECK(DecodeEventFrame(pair.network.PeekNext(kA, kB), retried));
  CHECK(retried.packet_id == packet_id);
  CHECK(retried.identity == identity);

  // Deliver to the real peer. Until its ACK arrives, sender stays pending.
  CHECK(pair.network.DeliverNext(kA, kB));
  CHECK(HasText(pair.b.sync->FindNode(node_id), "pending", kA, 2));
  state = SyncStateForPeer(pair.a.sync->FindNode(node_id), kB);
  CHECK(state->HasPendingEvent());
  CHECK(!state->HasDelivered(identity));
  CHECK(pair.network.PendingCount(kB, kA) >= 1);

  CHECK(pair.network.DeliverNext(kB, kA));
  state = SyncStateForPeer(pair.a.sync->FindNode(node_id), kB);
  CHECK(!state->HasPendingEvent());
  CHECK(state->HasDelivered(identity));

  // No duplicate apply on B; quiet after confirmation.
  CHECK(Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() == 2);
  auto world = pair.world();
  Settle(world);
  auto const quiet = pair.a.sends + pair.b.sends;
  for (int i = 0; i < 40; ++i) {
    Advance(pair.a, kShareOfferRetryIntervalUs + 1);
    Advance(pair.b, kShareOfferRetryIntervalUs + 2);
  }
  CHECK(pair.a.sends + pair.b.sends == quiet);
  CHECK(!Queued(world));
}


void TestLongOfflineMonth() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);
  auto const share_count = pair.a.sync->FindNode(node_id)->shares.size();
  CHECK(share_count == 2);
  auto const share_ids = SharesOf(pair.a.sync->FindNode(node_id));

  Offline(pair.network, kA, kB);
  Offline(pair.network, kB, kA);
  constexpr std::uint64_t kMonthUs =
      30ull * 24ull * 60ull * 60ull * 1'000'000ull;
  // Advance both clocks independently through a month of Service calls.
  // No outbound Send while Offline; no retry storm when Offline.
  auto const sends_a0 = pair.a.sends;
  auto const sends_b0 = pair.b.sends;
  for (int i = 0; i < 40; ++i) {
    Advance(pair.a, kMonthUs / 40 + static_cast<std::uint64_t>(i + 1));
    Advance(pair.b, kMonthUs / 40 + static_cast<std::uint64_t>(i + 7));
  }
  CHECK(pair.a.sends == sends_a0);
  CHECK(pair.b.sends == sends_b0);
  CHECK(pair.network.PendingCount(kA, kB) == 0);
  CHECK(pair.network.PendingCount(kB, kA) == 0);

  std::vector<std::pair<std::string, std::pair<std::string, std::uint64_t>>>
      oracle;
  oracle.push_back({"seed", {kA, 1}});
  for (std::uint64_t i = 1; i <= 30; ++i) {
    AddRecordLocal(*AsDialog(pair.a.sync->FindNode(node_id)),
                   "a-month-" + std::to_string(i), kA, i + 1);
    oracle.push_back({"a-month-" + std::to_string(i), {kA, i + 1}});
    AddRecordLocal(*AsDialog(pair.b.sync->FindNode(node_id)),
                   "b-month-" + std::to_string(i), kB, i);
    oracle.push_back({"b-month-" + std::to_string(i), {kB, i}});
  }
  // Still Offline: local history grows; no new Share.
  CHECK(pair.a.sync->FindNode(node_id)->shares.size() == 2);
  CHECK(pair.b.sync->FindNode(node_id)->shares.size() == 2);
  CHECK(SharesOf(pair.a.sync->FindNode(node_id))[0].share_id ==
        share_ids[0].share_id);
  CHECK(SharesOf(pair.a.sync->FindNode(node_id))[1].share_id ==
        share_ids[1].share_id);

  Online(pair.network, kA, kB);
  Online(pair.network, kB, kA);
  auto world = pair.world();
  Pump(world,
       [&] {
         return Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size() ==
                    oracle.size() &&
                Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() ==
                    oracle.size();
       },
       800);
  ExpectPairTopology(pair.a.sync->FindNode(node_id),
                     pair.b.sync->FindNode(node_id));
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
  for (auto const& entry : oracle) {
    CHECK(HasText(pair.a.sync->FindNode(node_id), entry.first,
                  entry.second.first, entry.second.second));
  }
  Settle(world);
  // After settle, advancing another month of retry intervals must not Send.
  auto const settled_sends = pair.a.sends + pair.b.sends;
  for (int i = 0; i < 200; ++i) {
    Advance(pair.a, kShareOfferRetryIntervalUs + 1);
    Advance(pair.b, kShareOfferRetryIntervalUs + 3);
  }
  CHECK(pair.a.sends + pair.b.sends == settled_sends);
  CHECK(!Queued(world));
}

void TestRestartsPreserveRelationship() {
  // During admission: holder restarts before NodeState is delivered.
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    CHECK(pair.network.PendingCount(kA, kB) == 1);
    NodeStateFrame pending;
    CHECK(DecodeNodeStateFrame(pair.network.PeekNext(kA, kB), pending));
    node = {};
    Restart(pair.a);
    pair.network.ClearQueues();
    ReopenDialog(pair.a, node_id);
    // Peer still expects the initial node after holder restart.
    pair.b.sync->ExpectInitialNodeFromEndpoint(kA, PairDialogNode::kClassId);
    auto state = SyncStateForPeer(pair.a.sync->FindNode(node_id), kB);
    CHECK(state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
    Advance(pair.a, 1);
    FinishPair(pair, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "after", kA, 2, 200);
    auto world = pair.world();
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "after", kA, 2);
         },
         80);
  }
  // After snapshot saved on B, before A's ACK is confirmed (drop ACK, restart B).
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.b.sync->FindNode(node_id).is_valid());
    CHECK(pair.network.PendingCount(kB, kA) == 1);
    node = {};
    Restart(pair.b);
    pair.network.ClearQueues();
    ReopenDialog(pair.b, node_id);
    CHECK(pair.b.binds == 0);
    auto binding = LoadBinding(pair.b);
    CHECK(binding->node_ids[0] == node_id.id());
    Advance(pair.a, kShareOfferRetryIntervalUs);
    FinishPair(pair, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "post", kA, 2, 300);
    auto world = pair.world();
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "post", kA, 2);
         },
         80);
  }
  // Unacked event.
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    FinishPair(pair, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "unacked", kA, 2, 400);
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(HasText(pair.b.sync->FindNode(node_id), "unacked", kA, 2));
    node = {};
    Restart(pair.a);
    pair.network.ClearQueues();
    ReopenDialog(pair.a, node_id);
    Advance(pair.a, 1);
    auto world = pair.world();
    Pump(world, [&] { return !Queued(world); }, 80);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "fresh", kA, 3, 500);
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "fresh", kA, 3);
         },
         80);
    ExpectSameObserve(pair.a.sync->FindNode(node_id),
                      pair.b.sync->FindNode(node_id));
  }
  // After saved ACK, both sides restart; reopen from storage only.
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    FormPair(pair, node);
    FinishPair(pair, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "acked", kA, 2, 600);
    auto world = pair.world();
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "acked", kA, 2);
         },
         80);
    Settle(world);
    auto const shares_before = SharesOf(pair.a.sync->FindNode(node_id));
    node = {};
    Restart(pair.a);
    Restart(pair.b);
    pair.network.ClearQueues();
    ReopenDialog(pair.a, node_id);
    ReopenDialog(pair.b, node_id);
    ExpectPairTopology(pair.a.sync->FindNode(node_id),
                       pair.b.sync->FindNode(node_id));
    CHECK(SharesOf(pair.a.sync->FindNode(node_id))[0].share_id ==
          shares_before[0].share_id);
    CHECK(pair.a.sync->FindNode(node_id)->shares.size() == 2);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "after-both-restart",
              kA, 3, 700);
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "after-both-restart",
                          kA, 3);
         },
         80);
  }
}

void TestIsolationTwoDialogs() {
  Trio trio;
  auto node_ab = MakeDialog(trio.a);
  auto node_ac = MakeDialog(trio.a);
  AddRecord(*node_ab, "ab-seed", kA, 1, 100);
  AddRecord(*node_ac, "ac-seed", kA, 1, 100);
  auto const ab_id = node_ab.id();
  auto const ac_id = node_ac.id();
  FormPair(trio.a, trio.b, node_ab);
  FormPair(trio.a, trio.c, node_ac);
  auto world = trio.world();
  Pump(world,
       [&] {
         auto ab_a = trio.a.sync->FindNode(ab_id);
         auto ab_b = trio.b.sync->FindNode(ab_id);
         auto ac_a = trio.a.sync->FindNode(ac_id);
         auto ac_c = trio.c.sync->FindNode(ac_id);
         if (!ab_a.is_valid() || !ab_b.is_valid() || !ac_a.is_valid() ||
             !ac_c.is_valid()) {
           return false;
         }
         return SyncStateForPeer(ab_a, kB)->GetInitialSyncPhase() ==
                    InitialSyncPhase::Complete &&
                SyncStateForPeer(ac_a, kC)->GetInitialSyncPhase() ==
                    InitialSyncPhase::Complete;
       },
       120);
  CHECK(trio.b.sync->FindNode(ab_id).is_valid());
  CHECK(!trio.b.sync->FindNode(ac_id).is_valid());
  CHECK(trio.c.sync->FindNode(ac_id).is_valid());
  CHECK(!trio.c.sync->FindNode(ab_id).is_valid());
  CHECK(trio.b.storage.Enumerate(ac_id).empty());
  CHECK(trio.c.storage.Enumerate(ab_id).empty());

  AddRecord(*AsDialog(trio.a.sync->FindNode(ab_id)), "to-b", kA, 2, 200);
  AddRecord(*AsDialog(trio.a.sync->FindNode(ac_id)), "to-c", kA, 2, 200);
  Offline(trio.network, kA, kB);
  Offline(trio.network, kB, kA);
  Pump(world,
       [&] {
         return HasText(trio.c.sync->FindNode(ac_id), "to-c", kA, 2);
       },
       80);
  CHECK(!HasText(trio.b.sync->FindNode(ab_id), "to-b", kA, 2));
  CHECK(!trio.b.sync->FindNode(ac_id).is_valid());
  Online(trio.network, kA, kB);
  Online(trio.network, kB, kA);
  Pump(world,
       [&] {
         return HasText(trio.b.sync->FindNode(ab_id), "to-b", kA, 2);
       },
       80);
  CHECK(!HasText(trio.b.sync->FindNode(ab_id), "to-c", kA, 2));
  CHECK(!HasText(trio.c.sync->FindNode(ac_id), "to-b", kA, 2));
}

void TestRejectThirdOnEstablishedPair() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);
  CHECK(pair.a.sync->FindNode(node_id)->shares.size() == 2);
  auto live = AsDialog(pair.a.sync->FindNode(node_id));
  live->InstallLocalShare(MakeLink(*pair.a.domain, kC), ShareAccess::ReadWrite);
  CHECK(live->shares.size() == 2);
  ExpectPairTopology(pair.a.sync->FindNode(node_id),
                     pair.b.sync->FindNode(node_id));
}

void TestNoExtraTrafficAfterSettleAndReopen() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);
  for (std::uint64_t i = 1; i <= 10; ++i) {
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "a" + std::to_string(i),
              kA, i + 1, 1000 + i);
    AddRecord(*AsDialog(pair.b.sync->FindNode(node_id)), "b" + std::to_string(i),
              kB, i, 2000 + i);
  }
  auto world = pair.world();
  Pump(world,
       [&] {
         return Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size() ==
                    21 &&
                Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() == 21;
       },
       400);
  Settle(world);
  auto const sends = pair.a.sends + pair.b.sends;
  for (int i = 0; i < 300; ++i) {
    Advance(pair.a, kShareOfferRetryIntervalUs + 1);
    Advance(pair.b, kShareOfferRetryIntervalUs + 2);
  }
  CHECK(pair.a.sends + pair.b.sends == sends);
  CHECK(!Queued(world));

  // Re-open dialog after restart must not replay the whole history.
  node = {};
  Restart(pair.a);
  Restart(pair.b);
  pair.network.ClearQueues();
  ReopenDialog(pair.a, node_id);
  ReopenDialog(pair.b, node_id);
  auto const reopen_sends = pair.a.sends + pair.b.sends;
  for (int i = 0; i < 100; ++i) {
    Advance(pair.a, kShareOfferRetryIntervalUs + 1);
    Advance(pair.b, kShareOfferRetryIntervalUs + 2);
  }
  CHECK(pair.a.sends + pair.b.sends == reopen_sends);
  CHECK(!Queued(world));
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
}

void TestEqualTimestampsObserveOracle() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);
  // Same timestamp_us from independent origins — contract does not invent a
  // second distributed order key. Observe uses (ts, origin, seq).
  AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "eq-a", kA, 2, 5000);
  AddRecord(*AsDialog(pair.b.sync->FindNode(node_id)), "eq-b", kB, 1, 5000);
  auto world = pair.world();
  Pump(world,
       [&] {
         return Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size() == 3 &&
                Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() == 3;
       },
       80);
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
  auto const seen = Observe(*AsDialog(pair.a.sync->FindNode(node_id)));
  CHECK(seen[1].timestamp_us == 5000);
  CHECK(seen[2].timestamp_us == 5000);
  CHECK(seen[1].id.origin_uid == kA);
  CHECK(seen[2].id.origin_uid == kB);
  // Materialized `records` follow Apply order and may differ from Observe when
  // timestamps collide; both sides must still hold the same identity set.
  auto const ra = AsDialog(pair.a.sync->FindNode(node_id))->records;
  auto const rb = AsDialog(pair.b.sync->FindNode(node_id))->records;
  CHECK(ra.size() == 3);
  CHECK(rb.size() == 3);
  std::set<std::string> set_a(ra.begin(), ra.end());
  std::set<std::string> set_b(rb.begin(), rb.end());
  CHECK(set_a == set_b);
  CHECK(set_a.count("eq-a") == 1);
  CHECK(set_a.count("eq-b") == 1);
}

void TestUnknownDoesNotBlockForever() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  FinishPair(pair, node_id);
  auto const sends_before = pair.a.sends;
  pair.network.SetAvailability(kA, kB, EndpointAvailability::Unknown);
  AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "via-unknown", kA, 2,
            200);
  ServiceRetry(pair.a, pair.network, kB);
  // Unknown must allow a first attempt (not permanently blocked).
  CHECK(pair.a.sends > sends_before);
  CHECK(pair.network.PendingCount(kA, kB) >= 1);
  Online(pair.network, kA, kB);
  Online(pair.network, kB, kA);
  auto world = pair.world();
  Pump(world,
       [&] {
         return HasText(pair.b.sync->FindNode(node_id), "via-unknown", kA, 2);
       },
       80);
}

struct OracleEntry {
  std::string text;
  std::string origin;
  std::uint64_t sequence{0};
  std::uint64_t timestamp_us{0};
};

void RunPermanentPairChaos(std::uint32_t seed, int steps,
                           int min_oracle_size) {
  std::mt19937 rng{seed};
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  FormPair(pair, node);
  node = {};
  auto world = pair.world();
  Pump(world, [&] { return PairJoined(pair, node_id); }, 80);

  // Forced Online after join must not count toward random-phase transitions.
  Online(pair.network, kA, kB);
  Online(pair.network, kB, kA);

  // Explicit Offline → Online recovery without restarting either replica.
  {
    Offline(pair.network, kA, kB);
    Offline(pair.network, kB, kA);
    AddRecordLocal(*AsDialog(pair.a.sync->FindNode(node_id)), "pre-offline-a",
                   kA, 2);
    AddRecordLocal(*AsDialog(pair.b.sync->FindNode(node_id)), "pre-offline-b",
                   kB, 1);
    auto const sends_a = pair.a.sends;
    auto const sends_b = pair.b.sends;
    Advance(pair.a, kShareOfferRetryIntervalUs);
    Advance(pair.b, kShareOfferRetryIntervalUs + 1);
    CHECK(pair.a.sends == sends_a);
    CHECK(pair.b.sends == sends_b);
    Online(pair.network, kA, kB);
    Online(pair.network, kB, kA);
    Pump(world,
         [&] {
           return HasText(pair.a.sync->FindNode(node_id), "pre-offline-b", kB,
                          1) &&
                  HasText(pair.b.sync->FindNode(node_id), "pre-offline-a", kA,
                          2);
         },
         120);
  }

  Replica* reps[2] = {&pair.a, &pair.b};
  std::string const ids[2] = {kA, kB};
  std::uint64_t seq[2] = {3, 2};
  std::vector<OracleEntry> oracle;
  oracle.push_back(
      OracleEntry{.text = "seed", .origin = kA, .sequence = 1, .timestamp_us = 100});
  // Capture whatever Observe holds after the recovery prelude (includes the
  // two offline messages with their real stamps).
  for (auto const& seen :
       Observe(*AsDialog(pair.a.sync->FindNode(node_id)))) {
    if (seen.text == "seed") {
      continue;
    }
    oracle.push_back(OracleEntry{.text = seen.text,
                                 .origin = seen.id.origin_uid,
                                 .sequence = seen.id.origin_sequence,
                                 .timestamp_us = seen.timestamp_us});
  }

  int transitions_to_offline = 0;
  int transitions_to_online = 0;

  auto write_one = [&](int who) {
    auto live = reps[who]->sync->FindNode(node_id);
    if (!live.is_valid()) {
      return;
    }
    auto dialog = AsDialog(live);
    auto const sequence = seq[who]++;
    auto const stamp = NextLocalStamp(*dialog);
    std::uint64_t ts = stamp;
    if ((rng() % 17) == 0) {
      ts = 10'000 + (rng() % 50);
    }
    auto const text = "m" + std::to_string(oracle.size()) + "-" + ids[who];
    AddRecord(*dialog, text, ids[who], sequence, ts);
    oracle.push_back(OracleEntry{.text = text,
                                 .origin = ids[who],
                                 .sequence = sequence,
                                 .timestamp_us = ts});
  };

  for (int step = 0; step < steps; ++step) {
    auto const roll = rng() % 100;
    auto const i = static_cast<int>(rng() % 2);
    auto const j = 1 - i;
    switch (roll % 6) {
      case 0:
        pair.network.DeliverNext(ids[i], ids[j]);
        break;
      case 1:
        pair.network.DropNext(ids[i], ids[j]);
        break;
      case 2:
        pair.network.DuplicateNext(ids[i], ids[j]);
        break;
      case 3:
        if (pair.network.PendingCount(ids[i], ids[j]) >= 2) {
          pair.network.DeferNext(ids[i], ids[j]);
        } else {
          pair.network.DeliverNext(ids[i], ids[j]);
        }
        break;
      case 4: {
        // Availability target is an independent draw — not coupled to roll%6
        // parity (which made Online unreachable when roll%6==4).
        auto const want = (rng() % 2) == 0 ? EndpointAvailability::Offline
                                           : EndpointAvailability::Online;
        SetAvailabilityTracked(pair.network, ids[i], ids[j], want,
                               transitions_to_offline, transitions_to_online);
        break;
      }
      default:
        DeliverRound(world);
        break;
    }

    if ((step % 3) == 0) {
      write_one(static_cast<int>(rng() % 2));
    }
    if ((step % 5) == 0) {
      write_one(0);
      write_one(1);
    }

    if ((step % 220) == 219) {
      reps[i]->Stop();
      pair.network.ClearQueues();
      reps[i]->Start();
      ReopenDialog(*reps[i], node_id);
      CHECK(reps[i]->sync->FindNode(node_id).is_valid());
      // Forced Online after restart is excluded from transition counters.
      Online(pair.network, kA, kB);
      Online(pair.network, kB, kA);
    }

    if ((step % 7) == 0) {
      reps[i]->clock +=
          kShareOfferRetryIntervalUs / 8 + static_cast<std::uint64_t>(i + 1);
      reps[i]->sync->Service(reps[i]->clock);
    }
  }

  CHECK(transitions_to_offline > 0);
  CHECK(transitions_to_online > 0);

  // Final delivery Online is forced and excluded from the counters above.
  Online(pair.network, kA, kB);
  Online(pair.network, kB, kA);

  bool settled = false;
  for (int step = 0; step < 2000; ++step) {
    Drain(world, 256);
    auto const sends = pair.a.sends + pair.b.sends;
    Advance(pair.a, kShareOfferRetryIntervalUs + 1);
    Advance(pair.b, kShareOfferRetryIntervalUs + 3);
    if (!Queued(world) && pair.a.sends + pair.b.sends == sends &&
        Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size() ==
            oracle.size() &&
        Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size() ==
            oracle.size()) {
      settled = true;
      break;
    }
  }
  if (!settled) {
    std::cerr << "chaos settle failed seed=" << seed
              << " oracle=" << oracle.size() << " a="
              << Observe(*AsDialog(pair.a.sync->FindNode(node_id))).size()
              << " b="
              << Observe(*AsDialog(pair.b.sync->FindNode(node_id))).size()
              << " to_offline=" << transitions_to_offline
              << " to_online=" << transitions_to_online << '\n';
    CHECK(false);
  }

  CHECK(static_cast<int>(oracle.size()) >= min_oracle_size);
  ExpectPairTopology(pair.a.sync->FindNode(node_id),
                     pair.b.sync->FindNode(node_id));
  ExpectSameObserve(pair.a.sync->FindNode(node_id),
                    pair.b.sync->FindNode(node_id));
  for (auto const& entry : oracle) {
    if (!HasText(pair.a.sync->FindNode(node_id), entry.text, entry.origin,
                 entry.sequence)) {
      std::cerr << "oracle miss seed=" << seed << " text=" << entry.text
                << " origin=" << entry.origin << " seq=" << entry.sequence
                << '\n';
      CHECK(false);
    }
  }

  auto const quiet = pair.a.sends + pair.b.sends;
  for (int i = 0; i < 80; ++i) {
    Advance(pair.a, kShareOfferRetryIntervalUs + 1);
    Advance(pair.b, kShareOfferRetryIntervalUs + 2);
  }
  CHECK(pair.a.sends + pair.b.sends == quiet);
  CHECK(!Queued(world));
}

void TestPermanentPairChaos() {
  // Several fixed seeds; total locally saved messages across runs >> 1000.
  RunPermanentPairChaos(0x3c1e1001u, 900, 400);
  RunPermanentPairChaos(0x3c1e1002u, 700, 300);
  RunPermanentPairChaos(0x3c1e1003u, 700, 300);
  RunPermanentPairChaos(0x3c1e1004u, 500, 200);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestFormationAndBasicExchange();
  apptraverse::test::TestLocalAcceptWhileOffline();
  apptraverse::test::TestLossesDuplicatesReorderDuringSync();
  apptraverse::test::TestWrongSourceDoesNotAck();
  apptraverse::test::TestLongOfflineMonth();
  apptraverse::test::TestRestartsPreserveRelationship();
  apptraverse::test::TestIsolationTwoDialogs();
  apptraverse::test::TestRejectThirdOnEstablishedPair();
  apptraverse::test::TestNoExtraTrafficAfterSettleAndReopen();
  apptraverse::test::TestEqualTimestampsObserveOracle();
  apptraverse::test::TestUnknownDoesNotBlockForever();
  apptraverse::test::TestPermanentPairChaos();
  std::cout << "permanent_pair_sync_test OK\n";
  return 0;
}
