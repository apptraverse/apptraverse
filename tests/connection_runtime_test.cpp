#include "apptraverse/connection_runtime.h"

#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>

#include "apptraverse/connection_hub.h"
#include "apptraverse/memory_link.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/process_services.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync.h"

#define CHECK(x)                                            \
  do {                                                      \
    if (!(x)) {                                             \
      std::cerr << "FAIL " #x " line " << __LINE__ << '\n'; \
      std::exit(1);                                         \
    }                                                       \
  } while (false)
using namespace apptraverse;
namespace {
class Transport final : public IConnectionTransport {
 public:
  MemoryLink link;
  NetworkObservation network;
  std::function<void()> on_service;
  bool bounded{false};
  int budget{1};
  explicit Transport(MemoryLinkNetwork& n, std::string id)
      : link{n, std::move(id)} {}
  void SetReceiver(Receiver r) override { link.SetReceiver(std::move(r)); }
  bool Send(std::string const& d, std::vector<std::uint8_t> const& b) override {
    if (bounded && budget-- <= 0) return false;
    return link.Send(d, b);
  }
  LinkAvailability Availability(std::string const&) const override {
    return LinkAvailability::Unknown;
  }
  void Service(ConnectionTime) override {
    budget = 1;
    if (on_service) on_service();
  }
  void NetworkChanged(NetworkObservation n) override { network = n; }
  bool Ready() const override { return true; }
  TransportPhase Phase() const override { return TransportPhase::Connected; }
  std::string const& EndpointId() const override { return link.EndpointId(); }
  std::string Name() const override { return "memory-router"; }
  bool WatchPeer(std::string const&) override { return true; }
};
class Presence final : public IPresenceSource {
 public:
  std::vector<PresenceReading> readings;
  std::vector<PresenceTarget> watched;
  NetworkObservation network;
  void Watch(PresenceTarget p) override { watched.push_back(p); }
  void Unwatch(PresenceTarget const& p) override { std::erase(watched, p); }
  void NetworkChanged(NetworkObservation n) override { network = n; }
  void Service(ConnectionTime) override {}
  std::vector<PresenceReading> Observations() const override {
    return readings;
  }
};
void IndependenceAndExpiry() {
  MemoryLinkNetwork wire;
  auto transport = std::make_unique<Transport>(wire, "router-A");
  auto* data = transport.get();
  auto presence = std::make_unique<Presence>();
  auto* source = presence.get();
  source->readings = {{{{PresenceScope::Endpoint, "ether-B"},
                        "independent-presence",
                        PresenceState::Online,
                        1700000000000000,
                        100},
                       100},
                      {{{PresenceScope::Application, "app-B"},
                        "heartbeat",
                        PresenceState::Offline,
                        1700000000000000,
                        100},
                       100}};
  ConnectionRuntime runtime{{std::move(transport), std::move(presence)}};
  runtime.WatchPresence({PresenceScope::Endpoint, "ether-B"});
  runtime.NetworkChanged({Reachability::Available, Reachability::Unavailable});
  CHECK(data->network.internet == Reachability::Unavailable);
  CHECK(source->network == data->network);
  MemoryLink peer{wire, "router-B"};
  int received = 0;
  peer.SetReceiver([&](auto const& sender, auto const& bytes) {
    CHECK(sender == "router-A");
    CHECK(bytes == std::vector<std::uint8_t>{42});
    ++received;
  });
  runtime.Service({199, 1700000000000099});
  auto snapshot = runtime.Observation();
  CHECK(snapshot.presence[0].state == PresenceState::Online);
  CHECK(snapshot.presence[1].state == PresenceState::Offline);
  runtime.Service({200, 1});
  snapshot =
      runtime.Observation();  // UTC jumped backwards; lease still expires.
  CHECK(snapshot.presence[0].state == PresenceState::Unknown);
  CHECK(snapshot.presence[1].state == PresenceState::Unknown);
  CHECK(runtime.Transport().Send("router-B", {42}));
  CHECK(wire.Deliver(wire.PendingPackets().front().id));
  CHECK(received == 1);
  CHECK(snapshot.phase == TransportPhase::Connected);
  std::cout << "PASS independent message/presence identities, LAN without "
               "Internet, monotonic expiry\n";
}

void ProcessHubChannels() {
  MemoryLinkNetwork wire;
  int factories = 0;
  Presence* source = nullptr;
  ConnectionHub left{[&] {
                       ++factories;
                       auto p = std::make_unique<Presence>();
                       source = p.get();
                       return ConnectionAdapters{
                           std::make_unique<Transport>(wire, "A"),
                           std::move(p)};
                     },
                     false};
  ConnectionHub right{[&] {
                        ++factories;
                        return ConnectionAdapters{
                            std::make_unique<Transport>(wire, "B"),
                            std::make_unique<Presence>()};
                      },
                      false};
  auto a1 = std::make_unique<ConnectionRuntime>(left.OpenChannel("document-1"));
  auto a2 = std::make_unique<ConnectionRuntime>(left.OpenChannel("document-2"));
  auto b1 =
      std::make_unique<ConnectionRuntime>(right.OpenChannel("document-1"));
  auto b2 =
      std::make_unique<ConnectionRuntime>(right.OpenChannel("document-2"));
  PresenceTarget target{PresenceScope::Application, "Editor"};
  a1->WatchPresence(target);
  a2->WatchPresence(target);
  int first = 0, second = 0;
  b1->Transport().SetReceiver([&](auto const&, auto const& b) {
    CHECK(b == std::vector<std::uint8_t>{1});
    ++first;
  });
  b2->Transport().SetReceiver([&](auto const&, auto const& b) {
    CHECK(b == std::vector<std::uint8_t>{2});
    ++second;
  });
  auto pump = [&](std::uint64_t t) {
    left.Service({t, 1000});
    right.Service({t, 1000});
    for (auto const& packet : wire.PendingPackets())
      CHECK(wire.Deliver(packet.id));
    if (a1) a1->Service({t, 1000});
    a2->Service({t, 1000});
    if (b1) b1->Service({t, 1000});
    b2->Service({t, 1000});
  };
  pump(100);
  CHECK(factories == 2);
  CHECK(source->watched.size() == 1);
  CHECK(a1->Transport().Send("B", {1}));
  CHECK(a2->Transport().Send("B", {2}));
  pump(200);
  CHECK(first == 1 && second == 1);
  b1.reset();
  CHECK(a1->Transport().Send("B", {1}));
  pump(300);
  CHECK(first == 1);  // closed channel never invokes its released receiver
  a1.reset();
  pump(400);
  CHECK(source->watched.size() == 1);
  a2->UnwatchPresence(target);
  pump(500);
  CHECK(source->watched.empty());
  a1 = std::make_unique<ConnectionRuntime>(left.OpenChannel("document-1"));
  b1 = std::make_unique<ConnectionRuntime>(right.OpenChannel("document-1"));
  b1->Transport().SetReceiver([&](auto const&, auto const&) { ++first; });
  pump(600);
  CHECK(a1->Transport().Send("B", {1}));
  pump(700);
  CHECK(first == 2 && second == 1);
  CHECK(factories == 2);
  std::cout << "PASS process connection reuse, channel isolation, shared "
               "watches, close/reopen without stale callbacks\n";
}

void FairChannels() {
  MemoryLinkNetwork wire;
  ConnectionHub hub{[&] {
                      auto t = std::make_unique<Transport>(wire, "A");
                      t->bounded = true;
                      return ConnectionAdapters{std::move(t),
                                                std::make_unique<Presence>()};
                    },
                    false};
  ConnectionHub peer{[&] {
                       return ConnectionAdapters{
                           std::make_unique<Transport>(wire, "B"),
                           std::make_unique<Presence>()};
                     },
                     false};
  ConnectionRuntime busy{hub.OpenChannel("a-busy")},
      quiet{hub.OpenChannel("b-quiet")}, receiver{peer.OpenChannel("b-quiet")};
  int received = 0;
  receiver.Transport().SetReceiver(
      [&](auto const&, auto const&) { ++received; });
  peer.Service({1, 1});
  for (int n = 0; n < 10; ++n) CHECK(busy.Transport().Send("B", {1}));
  CHECK(quiet.Transport().Send("B", {2}));
  for (std::uint64_t turn = 1; turn <= 2; ++turn) {
    hub.Service({turn, 1});
    for (auto const& packet : wire.PendingPackets())
      CHECK(wire.Deliver(packet.id));
    receiver.Service({turn, 1});
  }
  CHECK(received == 1);
  std::cout << "PASS bounded transport gives another graph a turn before "
               "draining a busy graph\n";
}

void ReopenDuringService() {
  MemoryLinkNetwork wire;
  std::mutex mutex;
  std::condition_variable wake;
  bool block = false, entered = false, released = false;
  ConnectionHub hub{[&] {
    auto transport = std::make_unique<Transport>(wire, "A");
    transport->on_service = [&] {
      std::unique_lock lock{mutex};
      if (!block) return;
      entered = true;
      wake.notify_all();
      wake.wait(lock, [&] { return released; });
    };
    return ConnectionAdapters{std::move(transport),
                              std::make_unique<Presence>()};
  }};
  auto old = hub.OpenChannel("graph");
  {
    std::lock_guard lock{mutex};
    block = true;
  }
  {
    std::unique_lock lock{mutex};
    CHECK(
        wake.wait_for(lock, std::chrono::seconds{3}, [&] { return entered; }));
  }
  old = {};  // the hub worker still holds a temporary Channel while adapters
             // unload
  auto reopened = hub.OpenChannel("graph");
  {
    std::lock_guard lock{mutex};
    released = true;
  }
  wake.notify_all();
  std::cout << "PASS immediate graph reopen while the process worker services "
               "the old channel\n";
}

ConnectionState::ptr State(ae::Domain& domain) {
  auto state = ConnectionState::ptr::Create(ae::CreateWith{domain});
  state->network = {Reachability::Unknown, Reachability::Unknown};
  state->session = {"", "", TransportPhase::Starting, {}, false, 0, 0};
  state->last_online.clear();
  state->now_utc_us = 0;
  InitializeRuntimeNode(*state);
  state->SetImmediateEventCompaction(true);
  return state;
}
void ModelAndProcessLifetime() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto first = State(domain), second = State(domain);
  int started = 0, stopped = 0;
  struct Observer : NetworkSubscription {
    int& stopped;
    explicit Observer(int& n) : stopped{n} {}
    ~Observer() { ++stopped; }
  };
  NetworkChanged os;
  {
    ProcessServices services{[&](NetworkChanged changed) {
                               ++started;
                               os = std::move(changed);
                               return std::make_unique<Observer>(stopped);
                             },
                             false};
    std::vector<ModelObjectProxy::ModelWork> queue;
    ModelObjectProxy proxy{
        [&](auto work) { queue.push_back(std::move(work)); }};
    auto flush = [&] {
      auto work = std::move(queue);
      queue.clear();
      for (auto& item : work) item(domain);
    };
    auto one =
        std::make_unique<ConnectionModelBinding>(services, proxy, first.id());
    auto two =
        std::make_unique<ConnectionModelBinding>(services, proxy, second.id());
    flush();
    os({Reachability::Available, Reachability::Unavailable});
    services.ObserveClock(5000000);
    CHECK(first->network.network == Reachability::Unknown);
    flush();
    CHECK(first->network == second->network);
    CHECK(first->now_utc_us == 5000000);
    CHECK(started == 1);
    os({Reachability::Unavailable, Reachability::Unavailable});
    services.ObserveClock(5500000);
    one.reset();
    flush();
    CHECK(first->network.network == Reachability::Available);
    CHECK(first->now_utc_us == 5000000);
    CHECK(second->now_utc_us == 5500000);
    os({Reachability::Unavailable, Reachability::Unavailable});
    services.ObserveClock(6000000);
    flush();
    CHECK(first->network.network == Reachability::Available);
    CHECK(first->now_utc_us == 5000000);
    CHECK(second->network.network == Reachability::Unavailable);
    CHECK(second->now_utc_us == 6000000);
    PresenceObservation p{{PresenceScope::Endpoint, "peer"},
                          "source",
                          PresenceState::Online,
                          4000000,
                          1000000};
    second->ObserveSession(
        {"router", "local", TransportPhase::Connected, {p}, true, 2, 1});
    CHECK(second->LastOnline(p.target, p.source) == 4000000);
    services.ObserveClock(999000000);
    flush();
    CHECK(second->LastOnline(p.target, p.source) == 4000000);
    p.state = PresenceState::Unknown;
    second->ObserveSession(
        {"router", "local", TransportPhase::Disconnected, {p}, true, 2, 1});
    ae::RamDomainStorage saved;
    SavePersistedObjectGraphToRam(*second, saved);
    ae::Domain restored{saved};
    ae::DomainGraph graph{&restored};
    auto held = graph.LoadRoot(second.id());
    CHECK(held);
    auto* state = held.as<ConnectionState>();
    CHECK(state->session == second->session);
    CHECK(state->last_online == second->last_online);
    state->SetImmediateEventCompaction(true);
    state->Reset();
    CHECK(state->session.phase == TransportPhase::Starting);
    CHECK(state->session.presence.empty());
    CHECK(state->LastOnline(p.target, p.source) == 4000000);
    CHECK(state->journal.empty());
    CHECK(second->journal.empty());
  }
  CHECK(started == 1 && stopped == 1);
  std::cout << "PASS one process observer, queued Events, subscription unload, "
               "last-seen persistence/reset\n";
}

class AddRecord;
class Catalog : public NodeFor<Catalog, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ConnectionCatalog", Catalog,
                           SharedNode, 0)
 protected:
  Catalog() = default;

 public:
  explicit Catalog(ae::ObjProp p) : NodeFor{p} {}
  std::vector<std::string> records;
  AE_OBJECT_REFLECT(AE_MMBR(records))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, records);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, records);
  }
  void Apply(AddRecord const&);
};
class AddRecord : public EventFor<Catalog, AddRecord> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ConnectionAddRecord", AddRecord,
                           Event, 0)
 protected:
  AddRecord() = default;

 public:
  explicit AddRecord(ae::ObjProp p) : EventFor{p} {}
  std::string name;
  AE_OBJECT_REFLECT(AE_MMBR(name))
  template <class D>
  void Load(ae::Version<0>, D& d) {
    d(base_, name);
  }
  template <class D>
  void Save(ae::Version<0>, D& d) const {
    d(base_, name);
  }
};
void Catalog::Apply(AddRecord const& e) {
  records.push_back(e.name);
  NoteMaterializedChange();
}
APPTRAVERSE_REGISTER(Catalog);
APPTRAVERSE_REGISTER(AddRecord);
void ArbitraryGraph() {
  MemoryLinkNetwork wire;
  ConnectionRuntime a{
      {std::make_unique<Transport>(wire, "A"), std::make_unique<Presence>()}};
  ConnectionRuntime b{
      {std::make_unique<Transport>(wire, "B"), std::make_unique<Presence>()}};
  ae::RamDomainStorage sa, sb, da, db;
  ae::Domain ma{sa}, mb{sb};
  auto catalog = Catalog::ptr::Create(ae::CreateWith{ma});
  catalog->records = {"Editor"};
  catalog->links.clear();
  for (auto name : {"A", "B"}) {
    auto l = SharedLinkDescriptor::ptr::Create(ae::CreateWith{ma});
    l->endpoint_uid = name;
    l->local_transport = {};
    InitializeRuntimeNode(*l);
    catalog->links.push_back(l);
  }
  SharedSyncRuntime::InitializeLocalState(*catalog);
  InitializeRuntimeNode(*catalog);
  SharedNode::ptr received;
  SharedSyncRuntime left{ma,
                         sa,
                         a.Transport(),
                         "A",
                         [&] {
                           SavePersistedObjectGraphToRam(*catalog, da);
                           return true;
                         },
                         100};
  SharedSyncRuntime right{mb,
                          sb,
                          b.Transport(),
                          "B",
                          [&] {
                            CHECK(received.is_valid());
                            SavePersistedObjectGraphToRam(*received, db);
                            return true;
                          },
                          100};
  left.RegisterNode(catalog);
  right.ExpectInitialState(catalog.id(), "A", Catalog::kClassId,
                           [&](auto root) { received = root; });
  CHECK(left.QueueInitialState(catalog.id(), "B"));
  auto pump = [&](std::uint64_t t) {
    a.Service({t, 1000});
    b.Service({t, 1000});
    left.Service(t);
    right.Service(t);
    for (int turn = 0; turn < 20; ++turn) {
      auto packets = wire.PendingPackets();
      if (packets.empty()) break;
      for (auto const& packet : packets) CHECK(wire.Deliver(packet.id));
      left.Service(t);
      right.Service(t);
    }
  };
  pump(100);
  CHECK(received.is_valid());
  CHECK(mb.Find(catalog.id()).as<Catalog>()->records == catalog->records);
  auto event = AddRecord::ptr::Create(ae::CreateWith{ma});
  event->name = "Calculator";
  auto id = event.id();
  CHECK(left.CommitLocal(catalog.id(), event, 1000));
  left.Service(200);
  auto packets = wire.PendingPackets();
  CHECK(!packets.empty());
  for (auto const& packet : packets) CHECK(wire.Drop(packet.id));
  pump(300);
  pump(400);
  CHECK(mb.Find(catalog.id()).as<Catalog>()->records ==
        std::vector<std::string>({"Editor", "Calculator"}));
  CHECK(catalog->local_state->peers.front()->HasDelivered(id));
  CHECK(!left.failed() && !right.failed());
  std::cout << "PASS application catalog graph over injected transport, "
               "dropped frame retry and durable ACK without presence\n";
}
}  // namespace
int main() {
  EnableNoninteractiveCrt();
  EnsureConnectionRegistration();
  EnsureSharedNodeRegistration();
  IndependenceAndExpiry();
  ProcessHubChannels();
  FairChannels();
  ReopenDuringService();
  ModelAndProcessLifetime();
  ArbitraryGraph();
}
