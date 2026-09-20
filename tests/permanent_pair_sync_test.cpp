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
    // Permanent-pair policy: accept the expected peer only; reject a third
    // participant on a node that already has two live shares.
    sync->SetShareOfferPolicy([this](SharedSyncRuntime::ShareOfferView const&
                                         offer) {
      if (offer.root_class_id != 0 &&
          offer.root_class_id != PairDialogNode::kClassId) {
        return false;
      }
      if (offer.kind == ShareOfferKind::Request) {
        auto node = sync->FindNode(offer.node_id);
        if (node.is_valid() && node->shares.size() >= 2) {
          return false;
        }
      }
      return true;
    });
    sync->SetLinkForEndpoint(
        [this](std::string const& endpoint) { return MakeLink(*domain, endpoint); });
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
    for (auto const& status : replica->sync->OfferStatuses()) {
      out += " op=";
      out += std::to_string(status.operation_id.id());
      out += " phase=";
      out += std::to_string(static_cast<int>(status.phase));
    }
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

bool PhaseIs(Replica& replica, ae::ObjId operation, ShareOfferPhase phase) {
  return replica.sync->OfferPhase(operation) == phase;
}

bool FullyJoined(Pair& pair, ae::ObjId operation, ae::ObjId node_id) {
  return PhaseIs(pair.a, operation, ShareOfferPhase::Complete) &&
         PhaseIs(pair.b, operation, ShareOfferPhase::Bound) &&
         pair.b.sync->FindNode(node_id).is_valid();
}

ae::ObjId OfferTo(Replica& holder, PairDialogNode::ptr node,
                  std::string const& remote_endpoint) {
  return holder.sync->OfferNode(node, MakeLink(*holder.domain, remote_endpoint),
                                ShareAccess::ReadWrite);
}

void FinishJoin(Pair& pair, ae::ObjId operation, ae::ObjId node_id) {
  Pump(pair.world(), [&] { return FullyJoined(pair, operation, node_id); }, 80);
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

// Re-open a saved dialog: restore runtime registration from storage/binding.
// Must not OfferNode or create a new journal.
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
  auto const op = OfferTo(pair.a, node, kB);
  auto const again = OfferTo(pair.a, AsDialog(pair.a.sync->FindNode(node_id)), kB);
  CHECK(op == again);
  CHECK(pair.a.sync->LocalOfferIds().size() == 1);
  FinishJoin(pair, op, node_id);
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

  // Idempotent Offer after Complete.
  auto const third =
      OfferTo(pair.a, AsDialog(pair.a.sync->FindNode(node_id)), kB);
  CHECK(third == op);
  CHECK(pair.a.sync->FindNode(node_id)->shares.size() == 2);
  CHECK(pair.a.sync->LocalOfferIds().size() == 1);
}

void TestLocalAcceptWhileOffline() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 10);
  auto const node_id = node.id();
  auto const op = OfferTo(pair.a, node, kB);
  FinishJoin(pair, op, node_id);
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
    auto const op = OfferTo(pair.a, node, kB);
    CHECK(pair.network.DropNext(kA, kB));
    ServiceRetry(pair.a, pair.network, kB);
    FinishJoin(pair, op, node_id);
  }
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    auto const op = OfferTo(pair.a, node, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.DropNext(kB, kA));
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.PendingCount(kB, kA) == 1);
    FinishJoin(pair, op, node_id);
  }
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    auto const op = OfferTo(pair.a, node, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.DeliverNext(kB, kA));
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
    FinishJoin(pair, op, node_id);
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
    auto const op = OfferTo(pair.a, node, kB);
    FinishJoin(pair, op, node_id);
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
    auto const op = OfferTo(pair.a, node, kB);
    FinishJoin(pair, op, node_id);
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
  auto const op = OfferTo(pair.a, node, kB);
  FinishJoin(pair, op, node_id);
  AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "pending", kA, 2, 200);
  ServiceRetry(pair.a, pair.network, kB);
  CHECK(pair.network.PendingCount(kA, kB) >= 1);
  EventFrame frame;
  CHECK(DecodeEventFrame(pair.network.PeekNext(kA, kB), frame));
  struct Eve {
    static void OnBytes(void*, std::string const&,
                        std::vector<std::uint8_t> const&) {}
  };
  MemoryTransport eve{pair.network, "endpoint-eve"};
  eve.BindReceive(nullptr, &Eve::OnBytes);
  AckFrame forged{.packet_id = frame.packet_id,
                  .target_node_id = frame.target_node_id,
                  .destination_share_id = frame.destination_share_id};
  eve.Send(kA, EncodeAckFrame(forged));
  CHECK(pair.network.DeliverNext("endpoint-eve", kA));
  // Pending must still be live; wrong source must not clear delivery.
  Advance(pair.a, kShareOfferRetryIntervalUs);
  CHECK(pair.network.PendingCount(kA, kB) >= 1);
  auto world = pair.world();
  Pump(world,
       [&] {
         return HasText(pair.b.sync->FindNode(node_id), "pending", kA, 2);
       },
       80);
}

void TestLongOfflineMonth() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  auto const op = OfferTo(pair.a, node, kB);
  FinishJoin(pair, op, node_id);
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
  // During admission.
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    auto const op = OfferTo(pair.a, node, kB);
    CHECK(pair.network.PendingCount(kA, kB) == 1);
    node = {};
    Restart(pair.a);
    pair.network.ClearQueues();
    CHECK(pair.a.sync->FindNode(node_id).is_valid());
    CHECK(PhaseIs(pair.a, op, ShareOfferPhase::Pending));
    Advance(pair.a, 1);
    FinishJoin(pair, op, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "after", kA, 2, 200);
    auto world = pair.world();
    Pump(world,
         [&] {
           return HasText(pair.b.sync->FindNode(node_id), "after", kA, 2);
         },
         80);
  }
  // After snapshot saved, before ACK.
  {
    Pair pair;
    auto node = MakeDialog(pair.a);
    AddRecord(*node, "seed", kA, 1, 100);
    auto const node_id = node.id();
    auto const op = OfferTo(pair.a, node, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.network.DeliverNext(kB, kA));
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(pair.b.sync->FindNode(node_id).is_valid());
    CHECK(PhaseIs(pair.b, op, ShareOfferPhase::Bound));
    CHECK(PhaseIs(pair.a, op, ShareOfferPhase::Accepted));
    node = {};
    Restart(pair.b);
    pair.network.ClearQueues();
    CHECK(pair.b.sync->FindNode(node_id).is_valid());
    CHECK(pair.b.binds == 0);
    auto binding = LoadBinding(pair.b);
    CHECK(binding->node_ids[0] == node_id.id());
    Advance(pair.a, kShareOfferRetryIntervalUs);
    FinishJoin(pair, op, node_id);
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
    auto const op = OfferTo(pair.a, node, kB);
    FinishJoin(pair, op, node_id);
    AddRecord(*AsDialog(pair.a.sync->FindNode(node_id)), "unacked", kA, 2, 400);
    ServiceRetry(pair.a, pair.network, kB);
    CHECK(pair.network.DeliverNext(kA, kB));
    CHECK(HasText(pair.b.sync->FindNode(node_id), "unacked", kA, 2));
    node = {};
    Restart(pair.a);
    pair.network.ClearQueues();
    CHECK(pair.a.sync->FindNode(node_id).is_valid());
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
    auto const op = OfferTo(pair.a, node, kB);
    FinishJoin(pair, op, node_id);
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
    CHECK(pair.a.sync->FindNode(node_id).is_valid());
    CHECK(pair.b.sync->FindNode(node_id).is_valid());
    ReopenDialog(pair.a, node_id);
    ReopenDialog(pair.b, node_id);
    ExpectPairTopology(pair.a.sync->FindNode(node_id),
                       pair.b.sync->FindNode(node_id));
    CHECK(SharesOf(pair.a.sync->FindNode(node_id))[0].share_id ==
          shares_before[0].share_id);
    // Re-open must not start a new Offer / journal.
    CHECK(pair.a.sync->LocalOfferIds().size() == 1);
    CHECK(OfferTo(pair.a, AsDialog(pair.a.sync->FindNode(node_id)), kB) == op);
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
  auto const op_b = OfferTo(trio.a, node_ab, kB);
  auto const op_c = OfferTo(trio.a, node_ac, kC);
  auto world = trio.world();
  Pump(world,
       [&] {
         return PhaseIs(trio.a, op_b, ShareOfferPhase::Complete) &&
                PhaseIs(trio.b, op_b, ShareOfferPhase::Bound) &&
                PhaseIs(trio.a, op_c, ShareOfferPhase::Complete) &&
                PhaseIs(trio.c, op_c, ShareOfferPhase::Bound);
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
  Trio trio;
  auto node = MakeDialog(trio.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  auto const op = OfferTo(trio.a, node, kB);
  auto world = trio.world();
  Pump(world,
       [&] {
         return PhaseIs(trio.a, op, ShareOfferPhase::Complete) &&
                PhaseIs(trio.b, op, ShareOfferPhase::Bound);
       },
       80);
  CHECK(trio.a.sync->FindNode(node_id)->shares.size() == 2);
  auto const to_c =
      trio.c.sync->RequestJoin(kA, node_id, ShareAccess::ReadWrite);
  Pump(world,
       [&] {
         return PhaseIs(trio.a, to_c, ShareOfferPhase::Rejected) ||
                PhaseIs(trio.c, to_c, ShareOfferPhase::Rejected);
       },
       80);
  CHECK(!trio.c.sync->FindNode(node_id).is_valid());
  CHECK(trio.c.storage.Enumerate(node_id).empty());
  CHECK(trio.a.sync->FindNode(node_id)->shares.size() == 2);
  ExpectPairTopology(trio.a.sync->FindNode(node_id),
                     trio.b.sync->FindNode(node_id));
}

void TestNoExtraTrafficAfterSettleAndReopen() {
  Pair pair;
  auto node = MakeDialog(pair.a);
  AddRecord(*node, "seed", kA, 1, 100);
  auto const node_id = node.id();
  auto const op = OfferTo(pair.a, node, kB);
  FinishJoin(pair, op, node_id);
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
  auto const op = OfferTo(pair.a, node, kB);
  FinishJoin(pair, op, node_id);
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
  auto const op = OfferTo(pair.a, node, kB);
  FinishJoin(pair, op, node_id);
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
  std::cout << "permanent_pair_sync_test (basic) OK\n";
  return 0;
}
