// Synthetic AppTraverse core stand: two replicas, each with its own Domain,
// in-memory storage and an ordinary persisted parent Node that references a
// child SharedNode. Only the child SharedNode and its shared journal
// replicate; the parents stay local and different. Replicas exchange nothing
// but serialized bytes over MemoryTransport.
//
// Not a chat test: no ChatSession, no GUI, no Aether network client, no
// sockets, no servers, no threads, no sleeps.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/link.h"
#include "apptraverse/memory_transport.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"
#include "apptraverse/sync_frame.h"

namespace apptraverse::test {
namespace {

// Live in Release too: NDEBUG must not turn these into no-ops.
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"   \
                << __LINE__ << '\n';                                     \
      std::exit(1);                                                      \
    }                                                                    \
  } while (0)

std::string const kHost = "stand-host";
std::string const kClient = "stand-client";

// Parent-local payload. A marker shared by both parents so one transport-level
// scan proves no local parent field ever reaches the wire.
std::string const kLocalMarker = "PARENT_LOCAL_SECRET";
std::string const kHostNote = kLocalMarker + "_host_only";
std::string const kClientNote = kLocalMarker + "_client_only";

ae::ObjId const kHostParentId{0x0C0FFE01};
ae::ObjId const kClientParentId{0x0C0FFE02};

// ---------------------------------------------------------------------------
// Model: one shared child Node and one ordinary local parent Node
// ---------------------------------------------------------------------------

class AppendLineEvent;

class SharedDoc : public NodeFor<SharedDoc, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::stand::SharedDoc", SharedDoc,
                           SharedNode, 0)

 protected:
  SharedDoc() = default;

 public:
  explicit SharedDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, lines);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, lines);
  }

  std::vector<std::string> lines;

  void Apply(AppendLineEvent const& event);
};

class AppendLineEvent : public EventFor<SharedDoc, AppendLineEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::stand::AppendLineEvent",
                           AppendLineEvent, Event, 0)

 protected:
  AppendLineEvent() = default;

 public:
  explicit AppendLineEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT()

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

void SharedDoc::Apply(AppendLineEvent const& event) {
  lines.push_back(event.text);
  NoteMaterializedChange();
}

class SetParentNoteEvent;
class LinkSharedDocEvent;

// Ordinary persisted Node. Never shared: it is not reachable from the child
// SharedNode, so it is not part of any snapshot.
class LocalParent : public NodeFor<LocalParent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::stand::LocalParent", LocalParent,
                           Node, 0)

 protected:
  LocalParent() = default;

 public:
  explicit LocalParent(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(shared))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, note, local_revision, shared);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, note, local_revision, shared);
  }

  std::string note;
  std::uint32_t local_revision{0};
  SharedDoc::ptr shared;

  void Apply(SetParentNoteEvent const& event);
  void Apply(LinkSharedDocEvent const& event);
};

class SetParentNoteEvent
    : public EventFor<LocalParent, SetParentNoteEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::stand::SetParentNoteEvent",
                           SetParentNoteEvent, Event, 0)

 protected:
  SetParentNoteEvent() = default;

 public:
  explicit SetParentNoteEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, note);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, note);
  }

  std::string note;
};

class LinkSharedDocEvent : public EventFor<LocalParent, LinkSharedDocEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::stand::LinkSharedDocEvent",
                           LinkSharedDocEvent, Event, 0)

 protected:
  LinkSharedDocEvent() = default;

 public:
  explicit LinkSharedDocEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(doc))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, doc);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, doc);
  }

  SharedDoc::ptr doc;
};

void LocalParent::Apply(SetParentNoteEvent const& event) {
  note = event.note;
  ++local_revision;
  NoteMaterializedChange();
}

void LocalParent::Apply(LinkSharedDocEvent const& event) {
  shared = event.doc;
  NoteMaterializedChange();
}

APPTRAVERSE_REGISTER(SharedDoc);
APPTRAVERSE_REGISTER(AppendLineEvent);
APPTRAVERSE_REGISTER(LocalParent);
APPTRAVERSE_REGISTER(SetParentNoteEvent);
APPTRAVERSE_REGISTER(LinkSharedDocEvent);

// ---------------------------------------------------------------------------
// Transport wrapper: counts Send calls, not queued packets
// ---------------------------------------------------------------------------

struct SendCounts {
  std::uint64_t total{0};
  std::uint64_t node_state{0};
  std::uint64_t event{0};
  std::uint64_t ack{0};
  // A frame whose (destination, type, packet_id) was already handed to Send.
  std::uint64_t retries{0};
  // Send issued while this transport observes the destination as Offline.
  std::uint64_t while_offline{0};
};

using SentKey = std::tuple<std::string, int, std::uint32_t>;

bool ContainsMarker(std::vector<std::uint8_t> const& bytes) {
  if (bytes.size() < kLocalMarker.size()) {
    return false;
  }
  std::string_view const view(reinterpret_cast<char const*>(bytes.data()),
                              bytes.size());
  return view.find(kLocalMarker) != std::string_view::npos;
}

// Counters and the sent-packet set live outside the transport so they survive
// a replica restart, which destroys the transport.
class CountingTransport final : public IByteTransport {
 public:
  CountingTransport(MemoryTransport& inner, SendCounts& counts,
                    std::set<SentKey>& sent)
      : inner_{inner}, counts_{counts}, sent_{sent} {}

  std::string const& local_endpoint_uid() const override {
    return inner_.local_endpoint_uid();
  }

  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override {
    ++counts_.total;
    if (inner_.Availability(destination_endpoint) ==
        EndpointAvailability::Offline) {
      ++counts_.while_offline;
    }
    CHECK(!ContainsMarker(bytes));

    SyncFrameType type{};
    CHECK(PeekSyncFrameType(bytes, type));
    ae::ObjId packet_id;
    switch (type) {
      case SyncFrameType::kNodeState: {
        NodeStateFrame frame;
        CHECK(DecodeNodeStateFrame(bytes, frame));
        packet_id = frame.packet_id;
        ++counts_.node_state;
        break;
      }
      case SyncFrameType::kEvent: {
        EventFrame frame;
        CHECK(DecodeEventFrame(bytes, frame));
        packet_id = frame.packet_id;
        ++counts_.event;
        break;
      }
      case SyncFrameType::kAck: {
        AckFrame frame;
        CHECK(DecodeAckFrame(bytes, frame));
        packet_id = frame.packet_id;
        ++counts_.ack;
        if (ack_send_hook_) {
          ack_send_hook_();
        }
        break;
      }
    }
    if (!sent_.insert(SentKey{destination_endpoint, static_cast<int>(type),
                              packet_id.id()})
             .second) {
      ++counts_.retries;
    }
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
    availability_ctx_ = ctx;
    availability_fn_ = fn;
    inner_.BindAvailability(ctx, fn);
  }
  void ClearAvailability() override {
    availability_ctx_ = nullptr;
    availability_fn_ = nullptr;
    inner_.ClearAvailability();
  }

  // Repeat an availability notification the transport already delivered, with
  // the observed value unchanged. Real transports do this whenever the peer
  // re-announces itself.
  void RepeatAvailabilityNotification(std::string const& endpoint) {
    CHECK(availability_fn_ != nullptr);
    availability_fn_(availability_ctx_, endpoint, inner_.Availability(endpoint));
  }

  // Run before an Ack frame leaves this endpoint, while the sending call is
  // still on the stack.
  void SetAckSendHook(std::function<void()> hook) {
    ack_send_hook_ = std::move(hook);
  }

 private:
  MemoryTransport& inner_;
  SendCounts& counts_;
  std::set<SentKey>& sent_;
  std::function<void()> ack_send_hook_;
  void* availability_ctx_{nullptr};
  AvailabilityFn availability_fn_{nullptr};
};

// ---------------------------------------------------------------------------
// One replica: own storage, Domain, transport endpoint, sync runtime
// ---------------------------------------------------------------------------

struct Replica;

// Defined with the oracles below: what must already be true of this replica's
// own storage whenever it acknowledges.
void ExpectAckPreconditions(Replica& replica);

struct Replica {
  Replica(MemoryNetwork& network, std::string endpoint, std::string peer,
          ae::ObjId parent_id, std::string note)
      : parent_id_{parent_id},
        note_{std::move(note)},
        network_{network},
        endpoint_{std::move(endpoint)},
        peer_{std::move(peer)} {}

  // Everything except the in-memory storage is built here and destroyed by
  // Stop(); a restart therefore recovers from persisted bytes only.
  void Start() {
    domain = std::make_unique<ae::Domain>(storage);
    transport = std::make_unique<MemoryTransport>(network_, endpoint_);
    counting = std::make_unique<CountingTransport>(*transport, counts, sent_);
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, *counting);
    sync->AllowStandaloneEventClass(AppendLineEvent::kClassId);
    sync->SetInitialNodeImportedCallback(
        [this](std::string const& source_endpoint, SharedNode::ptr node) {
          return AdoptImportedDoc(source_endpoint, node);
        });
    counting->SetAckSendHook([this] { ExpectAckPreconditions(*this); });
  }

  // Nothing a restarted application could only know from RAM may survive:
  // the live parent handle, the child id and the origin-sequence cache all go
  // with the runtime. Only the storage and the test's own observation
  // counters stay.
  void Stop() {
    parent_ = {};
    doc_id = {};
    sequence_cache_ = 0;
    sync.reset();
    counting.reset();
    transport.reset();
    domain.reset();
  }

  std::string const& endpoint() const { return endpoint_; }
  std::string const& peer() const { return peer_; }
  ae::ObjId parent_id() const { return parent_id_; }

  // The live parent of this session, loaded from storage on first use.
  LocalParent::ptr Parent() const {
    if (!parent_.is_valid()) {
      auto parent = LocalParent::ptr::Declare(
          ae::CreateWith{*domain}.with_id(parent_id_));
      parent.Load();
      CHECK(parent.is_loaded());
      parent_ = parent;
    }
    return parent_;
  }

  SharedDoc::ptr Doc() const {
    CHECK(doc_id.is_valid());
    SharedDoc::ptr doc = sync->FindNode(doc_id);
    if (doc.is_valid() && !doc.is_loaded()) {
      doc.Load();
    }
    return doc;
  }

  // Persist the whole local tree: the parent subtree by graph traversal, plus
  // the LocalPtr sync states the traversal deliberately does not follow.
  void SaveTree() const {
    Parent().Save();
    auto doc = Doc();
    if (!doc.is_valid()) {
      return;
    }
    for (auto& state : doc->link_sync_states) {
      state.Save();
    }
  }

  LocalParent::ptr CreateParent() {
    auto parent =
        LocalParent::ptr::Create(ae::CreateWith{*domain}.with_id(parent_id_));
    InitializeRuntimeNode(*parent);
    parent_ = parent;
    auto event = SetParentNoteEvent::ptr::Create(ae::CreateWith{*domain});
    event->note = note_;
    parent->Commit(event);
    parent.Save();
    return parent;
  }

  // Host side: create the child SharedNode with the permanent pair topology
  // and attach it to the local parent through an ordinary local Event.
  SharedDoc::ptr CreateDoc() {
    auto doc = SharedDoc::ptr::Create(ae::CreateWith{*domain});
    InitializeRuntimeNode(*doc);
    doc->InstallLocalShare(MakeLink(endpoint_), ShareAccess::ReadWrite);
    doc->InstallLocalShare(MakeLink(peer_), ShareAccess::ReadWrite);
    CHECK(doc->shares.size() == 2);
    doc_id = doc.id();
    AttachDocToParent(doc);
    sync->RegisterNode(doc);
    SaveTree();
    return doc;
  }

  // Origin sequence of the next local shared Event. After a restart the cache
  // is empty and the value is recovered from the persisted shared journal:
  // this replica's own highest origin_sequence plus one.
  std::uint64_t NextSharedSequence() {
    if (sequence_cache_ == 0) {
      sequence_cache_ = HighestPersistedOriginSequence();
    }
    return ++sequence_cache_;
  }

  // Runtime-only view of that counter. Zero means "not recovered yet".
  std::uint64_t CachedSequence() const { return sequence_cache_; }

  // Restore after a full unload: load the parent from storage, reach the child
  // through the persisted reference, register it with the ordinary API.
  ae::ObjId RestoreFromStorage() {
    CHECK(sync->RegisteredNodeIds().empty());
    auto parent = Parent();
    CHECK(parent->shared.is_valid());
    if (!parent->shared.is_loaded()) {
      parent->shared.Load();
    }
    CHECK(parent->shared.is_loaded());
    SharedDoc::ptr doc = parent->shared;
    doc_id = doc.id();
    sync->RegisterNode(doc);
    return doc.id();
  }

  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  std::unique_ptr<MemoryTransport> transport;
  std::unique_ptr<CountingTransport> counting;
  std::unique_ptr<SharedSyncRuntime> sync;

  SendCounts counts;
  std::uint64_t clock{0};
  int imports{0};
  ae::ObjId doc_id;

 private:
  std::uint64_t HighestPersistedOriginSequence() const {
    auto doc = Doc();
    CHECK(doc.is_valid());
    std::uint64_t highest = 0;
    for (auto const& record : doc->journal) {
      if (!record.HasSharedIdentity() ||
          record.identity.origin_uid != endpoint_) {
        continue;
      }
      highest = std::max(highest, record.identity.origin_sequence);
    }
    return highest;
  }

  MemoryLink::ptr MakeLink(std::string endpoint) const {
    auto link = MemoryLink::ptr::Create(
        ae::CreateWith{*domain}.with_id(ae::ObjId::GenerateUnique()));
    link->endpoint_uid = std::move(endpoint);
    link->heartbeat_interval_ms = 1000;
    InitializeRuntimeNode(*link);
    link.Save();
    return link;
  }

  void AttachDocToParent(SharedDoc::ptr doc) const {
    auto parent = Parent();
    auto event = LinkSharedDocEvent::ptr::Create(ae::CreateWith{*domain});
    event->doc = doc;
    parent->Commit(event);
  }

  bool AdoptImportedDoc(std::string const& source_endpoint,
                        SharedNode::ptr node) {
    if (source_endpoint != peer_ || !node.is_valid()) {
      return false;
    }
    if (ae::Registry::GetRegistry().GenerationDistance(
            SharedDoc::kClassId, node->GetClassId()) < 0) {
      return false;
    }
    SharedDoc::ptr doc = node;
    doc.Load();
    if (!doc.is_loaded()) {
      return false;
    }
    auto parent = Parent();
    if (parent->shared.is_valid() && parent->shared.id() != doc.id()) {
      return false;
    }
    doc_id = doc.id();
    AttachDocToParent(doc);
    SaveTree();
    ++imports;
    return true;
  }

  ae::ObjId parent_id_;
  mutable LocalParent::ptr parent_;
  std::uint64_t sequence_cache_{0};
  std::string note_;
  std::set<SentKey> sent_;
  MemoryNetwork& network_;
  std::string endpoint_;
  std::string peer_;
};

struct Stand {
  Stand()
      : host{network, kHost, kClient, kHostParentId, kHostNote},
        client{network, kClient, kHost, kClientParentId, kClientNote} {
    host.Start();
    client.Start();
  }

  std::uint64_t NextTimestamp() { return ++timestamp_us; }

  MemoryNetwork network;
  Replica host;
  Replica client;
  // Test-side logical clock for shared event order: strictly increasing and
  // globally unique, so both journals have one unambiguous order.
  std::uint64_t timestamp_us{1000};
};

// ---------------------------------------------------------------------------
// Deterministic driving: no threads, no sleeps, no real network waits
// ---------------------------------------------------------------------------

bool DeliverRound(Stand& stand) {
  bool moved = false;
  if (stand.network.DeliverNext(kHost, kClient)) {
    moved = true;
  }
  if (stand.network.DeliverNext(kClient, kHost)) {
    moved = true;
  }
  return moved;
}

void ServiceBoth(Stand& stand, std::uint64_t delta_us) {
  stand.host.clock += delta_us;
  stand.host.sync->Service(stand.host.clock);
  stand.client.clock += delta_us + 1;
  stand.client.sync->Service(stand.client.clock);
}

void Advance(Replica& replica, std::uint64_t delta_us) {
  replica.clock += delta_us;
  replica.sync->Service(replica.clock);
}

std::uint64_t TotalSends(Stand const& stand) {
  return stand.host.counts.total + stand.client.counts.total;
}

bool Queued(Stand const& stand) {
  return stand.network.PendingCount(kHost, kClient) != 0 ||
         stand.network.PendingCount(kClient, kHost) != 0;
}

template <typename Done>
void Pump(Stand& stand, Done&& done, int max_steps) {
  for (int step = 0; step < max_steps; ++step) {
    if (done()) {
      return;
    }
    DeliverRound(stand);
    ServiceBoth(stand, kShareOfferRetryIntervalUs / 4 + 1);
    if (done()) {
      return;
    }
  }
  std::cerr << "pump did not reach the expected state\n";
  CHECK(false);
}

void Settle(Stand& stand, int max_steps = 200) {
  for (int step = 0; step < max_steps; ++step) {
    auto const before = TotalSends(stand);
    while (DeliverRound(stand)) {
    }
    ServiceBoth(stand, kShareOfferRetryIntervalUs + 1);
    if (!Queued(stand) && TotalSends(stand) == before) {
      return;
    }
  }
  std::cerr << "stand did not settle\n";
  CHECK(false);
}

// Nothing left to do: further Service calls, however far time moves, send no
// synchronization frames at all.
void ExpectQuiet(Stand& stand, int rounds = 40) {
  Settle(stand);
  auto const quiet = TotalSends(stand);
  for (int i = 0; i < rounds; ++i) {
    Advance(stand.host, kShareOfferRetryIntervalUs + 1);
    Advance(stand.client, kShareOfferRetryIntervalUs + 3);
  }
  CHECK(TotalSends(stand) == quiet);
  CHECK(!Queued(stand));
}

void SetAvailability(Stand& stand, std::string const& from,
                     std::string const& to, EndpointAvailability availability) {
  stand.network.SetAvailability(from, to, availability);
}

// ---------------------------------------------------------------------------
// Oracles: identity, data and order of shared events plus materialized state
// ---------------------------------------------------------------------------

struct Observed {
  SharedEventId id;
  std::uint64_t timestamp_us{0};
  std::string text;
};

// Journal order as stored. Nothing is re-sorted here: a divergence in stored
// order must show up as a failure, not be normalized away.
std::vector<Observed> Observe(SharedDoc const& doc) {
  std::vector<Observed> out;
  for (auto const& record : doc.journal) {
    if (!record.HasSharedIdentity()) {
      continue;
    }
    auto event = record.event;
    CHECK(event.is_valid());
    if (!event.is_loaded()) {
      event.Load();
    }
    CHECK(event.is_loaded());
    if (event->GetClassId() != AppendLineEvent::kClassId) {
      continue;
    }
    AppendLineEvent::ptr concrete = event;
    CHECK(concrete.is_loaded());
    out.push_back(Observed{.id = record.identity,
                           .timestamp_us = record.order.timestamp_us,
                           .text = concrete->text});
  }
  return out;
}

void ExpectNoDuplicates(std::vector<Observed> const& seen) {
  std::set<std::pair<std::string, std::uint64_t>> ids;
  for (auto const& entry : seen) {
    CHECK(ids.insert({entry.id.origin_uid, entry.id.origin_sequence}).second);
  }
}

// Materialized state must be exactly the journal replayed in stored order.
void ExpectMaterializedFollowsJournal(SharedDoc const& doc) {
  auto const seen = Observe(doc);
  CHECK(doc.lines.size() == seen.size());
  for (std::size_t i = 0; i < seen.size(); ++i) {
    CHECK(doc.lines[i] == seen[i].text);
  }
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
  return out;
}

void ExpectSameDialog(Stand& stand) {
  auto host_doc = stand.host.Doc();
  auto client_doc = stand.client.Doc();
  CHECK(host_doc.is_valid());
  CHECK(client_doc.is_valid());
  CHECK(host_doc.id() == client_doc.id());
  // Independent C++ instances in independent Domains.
  CHECK(static_cast<void const*>(&*host_doc) !=
        static_cast<void const*>(&*client_doc));

  auto const host_shares = SharesOf(host_doc);
  auto const client_shares = SharesOf(client_doc);
  CHECK(host_shares.size() == 2);
  CHECK(client_shares.size() == 2);
  for (std::size_t i = 0; i < host_shares.size(); ++i) {
    CHECK(host_shares[i].share_id == client_shares[i].share_id);
    CHECK(host_shares[i].link_id == client_shares[i].link_id);
    CHECK(host_shares[i].endpoint == client_shares[i].endpoint);
    CHECK(host_shares[i].access == ShareAccess::ReadWrite);
    CHECK(client_shares[i].access == ShareAccess::ReadWrite);
  }

  auto const host_seen = Observe(*host_doc);
  auto const client_seen = Observe(*client_doc);
  CHECK(host_seen.size() == client_seen.size());
  for (std::size_t i = 0; i < host_seen.size(); ++i) {
    CHECK(host_seen[i].id == client_seen[i].id);
    CHECK(host_seen[i].timestamp_us == client_seen[i].timestamp_us);
    CHECK(host_seen[i].text == client_seen[i].text);
  }
  ExpectNoDuplicates(host_seen);
  ExpectNoDuplicates(client_seen);
  ExpectMaterializedFollowsJournal(*host_doc);
  ExpectMaterializedFollowsJournal(*client_doc);
}

// Parents are local: different objects, different content, never in the other
// replica's storage, and untouched by synchronization.
void ExpectParentsIndependent(Stand& stand) {
  auto host_parent = stand.host.Parent();
  auto client_parent = stand.client.Parent();
  CHECK(host_parent.id() == kHostParentId);
  CHECK(client_parent.id() == kClientParentId);
  CHECK(host_parent->note == kHostNote);
  CHECK(client_parent->note == kClientNote);
  CHECK(host_parent->note != client_parent->note);
  CHECK(stand.host.storage.Enumerate(kClientParentId).empty());
  CHECK(stand.client.storage.Enumerate(kHostParentId).empty());
  CHECK(host_parent->shared.is_valid());
  CHECK(client_parent->shared.is_valid());
  CHECK(host_parent->shared.id() == client_parent->shared.id());
  CHECK(host_parent->shared.id() == stand.host.Doc().id());
}

std::size_t CountLocalEvents(LocalParent const& parent) {
  std::size_t count = 0;
  for (auto const& record : parent.journal) {
    if (!record.HasSharedIdentity()) {
      ++count;
    }
  }
  return count;
}

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
    auto const index = node->FindLinkSyncIndexForShare(share.share_id);
    CHECK(index < node->link_sync_states.size());
    auto state = node->link_sync_states[index];
    if (!state.is_loaded()) {
      state.Load();
    }
    CHECK(state.is_loaded());
    return state;
  }
  CHECK(false);
  return {};
}

// What a cold reader finds in a replica's storage right now, reached the way
// a restart reaches it: parent first, child only through its saved reference.
// Nothing of the live session is consulted.
struct PersistedView {
  std::vector<std::string> lines;
  std::set<std::pair<std::string, std::uint64_t>> identities;
};

PersistedView ReadPersisted(Replica& replica) {
  ae::Domain probe_domain{replica.storage};
  auto parent = LocalParent::ptr::Declare(
      ae::CreateWith{probe_domain}.with_id(replica.parent_id()));
  parent.Load();
  CHECK(parent.is_loaded());
  PersistedView view;
  if (!parent->shared.is_valid()) {
    return view;
  }
  if (!parent->shared.is_loaded()) {
    parent->shared.Load();
  }
  CHECK(parent->shared.is_loaded());
  SharedDoc::ptr doc = parent->shared;
  view.lines = doc->lines;
  for (auto const& seen : Observe(*doc)) {
    view.identities.insert({seen.id.origin_uid, seen.id.origin_sequence});
  }
  return view;
}

// An ACK is a statement about durable state, so it may only leave once the
// events it covers are in this replica's own storage. Checked while the Send
// call is still on the stack: the live shared journal must already be fully
// covered by what a cold reader of the storage would find, and the persisted
// materialized state must match the live one.
void ExpectAckPreconditions(Replica& replica) {
  if (!replica.doc_id.is_valid()) {
    return;
  }
  auto doc = replica.Doc();
  if (!doc.is_valid()) {
    return;
  }
  auto const persisted = ReadPersisted(replica);
  for (auto const& seen : Observe(*doc)) {
    CHECK(persisted.identities.count(
              {seen.id.origin_uid, seen.id.origin_sequence}) == 1);
  }
  CHECK(persisted.lines == doc->lines);
}

bool HasLine(SharedDoc::ptr doc, std::string const& text) {
  if (!doc.is_valid()) {
    return false;
  }
  for (auto const& seen : Observe(*doc)) {
    if (seen.text == text) {
      return true;
    }
  }
  return false;
}

// One shared write on one replica, persisted locally before anything is sent.
std::string Write(Stand& stand, Replica& who, std::string text) {
  auto doc = who.Doc();
  CHECK(doc.is_valid());
  auto const journal_before = doc->journal.size();
  auto event = AppendLineEvent::ptr::Create(ae::CreateWith{*who.domain});
  event->text = text;
  auto const sequence = who.NextSharedSequence();
  doc->CommitShared(event,
                    SharedEventId{.origin_uid = who.endpoint(),
                                  .origin_sequence = sequence},
                    SharedEventOrder{.timestamp_us = stand.NextTimestamp()});
  // A reused identity is refused by the journal instead of throwing: the
  // commit must be visible, not silently dropped under NDEBUG.
  CHECK(doc->journal.size() == journal_before + 1);
  who.SaveTree();
  return text;
}

void Report(char const* scenario, Stand const& stand) {
  std::cout << scenario << ": sends host=" << stand.host.counts.total
            << " client=" << stand.client.counts.total
            << " | initial_snapshots=" << stand.host.counts.node_state << "/"
            << stand.client.counts.node_state
            << " events=" << stand.host.counts.event << "/"
            << stand.client.counts.event
            << " acks=" << stand.host.counts.ack << "/"
            << stand.client.counts.ack
            << " retries=" << stand.host.counts.retries << "/"
            << stand.client.counts.retries
            << " offline_sends=" << stand.host.counts.while_offline << "/"
            << stand.client.counts.while_offline << '\n';
}

// ---------------------------------------------------------------------------
// Scenario 1: first connection
// ---------------------------------------------------------------------------

// Host holds a saved parent + child SharedNode with history; the client has
// only its own parent and permits the expected host.
void Connect(Stand& stand) {
  stand.host.CreateParent();
  stand.client.CreateParent();
  auto doc = stand.host.CreateDoc();
  auto const doc_id = doc.id();

  Write(stand, stand.host, "h1");
  Write(stand, stand.host, "h2");
  Write(stand, stand.host, "h3");
  doc = {};

  CHECK(stand.client.storage.Enumerate(doc_id).empty());
  CHECK(stand.client.sync->RegisteredNodeIds().empty());
  CHECK(!stand.client.Parent()->shared.is_valid());

  stand.client.sync->ExpectInitialNodeFromEndpoint(kHost, SharedDoc::kClassId);

  // Nothing is known about the peer yet: Unknown must still allow the first
  // attempt.
  CHECK(stand.network.Availability(kHost, kClient) ==
        EndpointAvailability::Unknown);
  CHECK(stand.host.counts.total == 0);
  Advance(stand.host, 1);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.host.counts.total == 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);

  Pump(stand,
       [&] {
         auto state = stand.host.sync->FindNode(doc_id);
         return state.is_valid() &&
                SyncStateForPeer(state, kClient)->GetInitialSyncPhase() ==
                    InitialSyncPhase::Complete;
       },
       40);

  CHECK(stand.client.imports == 1);
  CHECK(stand.client.doc_id == doc_id);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.client.counts.node_state == 0);
}

void TestFirstConnection() {
  Stand stand;
  Connect(stand);
  auto const doc_id = stand.host.doc_id;

  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);

  auto client_doc = stand.client.Doc();
  CHECK(client_doc->lines.size() == 3);
  CHECK(client_doc->lines[0] == "h1");
  CHECK(client_doc->lines[2] == "h3");

  // The restored link is persisted on the client, not just live in memory.
  CHECK(!stand.client.storage.Enumerate(kClientParentId).empty());
  CHECK(!stand.client.storage.Enumerate(doc_id).empty());
  client_doc = {};
  {
    ae::Domain probe_domain{stand.client.storage};
    auto probe = LocalParent::ptr::Declare(
        ae::CreateWith{probe_domain}.with_id(kClientParentId));
    probe.Load();
    CHECK(probe.is_loaded());
    CHECK(probe->note == kClientNote);
    CHECK(probe->shared.is_valid());
    CHECK(probe->shared.id() == doc_id);
    if (!probe->shared.is_loaded()) {
      probe->shared.Load();
    }
    CHECK(probe->shared.is_loaded());
    CHECK(probe->shared->lines.size() == 3);
  }

  ExpectQuiet(stand);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  Report("scenario 1 first connection", stand);
}

// ---------------------------------------------------------------------------
// Scenario 2: incremental synchronization
// ---------------------------------------------------------------------------

void TestIncrementalSync() {
  Stand stand;
  Connect(stand);
  auto const after_connect_snapshots = stand.host.counts.node_state;

  std::vector<std::string> expected{"h1", "h2", "h3"};
  for (int round = 0; round < 4; ++round) {
    expected.push_back(
        Write(stand, stand.host, "host-" + std::to_string(round)));
    expected.push_back(
        Write(stand, stand.client, "client-" + std::to_string(round)));
    Pump(stand,
         [&] {
           return stand.host.Doc()->lines.size() == expected.size() &&
                  stand.client.Doc()->lines.size() == expected.size();
         },
         60);
  }

  ExpectSameDialog(stand);
  CHECK(stand.host.Doc()->lines == expected);
  CHECK(stand.client.Doc()->lines == expected);
  // No second full snapshot in either direction.
  CHECK(stand.host.counts.node_state == after_connect_snapshots);
  CHECK(stand.client.counts.node_state == 0);
  // Every shared event travelled exactly once per direction.
  CHECK(stand.host.counts.event == 4);
  CHECK(stand.client.counts.event == 4);
  CHECK(stand.host.counts.retries == 0);
  CHECK(stand.client.counts.retries == 0);

  ExpectParentsIndependent(stand);
  ExpectQuiet(stand);
  Report("scenario 2 incremental", stand);
}

// ---------------------------------------------------------------------------
// Scenario 3: offline without unload
// ---------------------------------------------------------------------------

// sender observes receiver Offline, keeps writing, and resumes on Online
// without a restart and without a second snapshot.
void RunOfflineWithoutUnload(Stand& stand, Replica& sender, Replica& receiver,
                             std::string const& prefix) {
  auto const snapshots_before = sender.counts.node_state;
  void const* receiver_doc_before =
      static_cast<void const*>(&*receiver.Doc());

  SetAvailability(stand, sender.endpoint(), receiver.endpoint(),
                  EndpointAvailability::Offline);

  std::vector<std::string> written;
  for (int i = 0; i < 3; ++i) {
    written.push_back(
        Write(stand, sender, prefix + "-offline-" + std::to_string(i)));
  }

  auto const sends_before = sender.counts.total;
  // Far past every retry deadline: an Offline destination gets no Send call.
  for (int i = 0; i < 25; ++i) {
    Advance(sender, kShareOfferRetryIntervalUs * 3 + 1);
    DeliverRound(stand);
  }
  CHECK(sender.counts.total == sends_before);
  CHECK(sender.counts.while_offline == 0);
  CHECK(stand.network.PendingCount(sender.endpoint(), receiver.endpoint()) == 0);
  for (auto const& text : written) {
    CHECK(HasLine(sender.Doc(), text));
    CHECK(!HasLine(receiver.Doc(), text));
  }

  SetAvailability(stand, sender.endpoint(), receiver.endpoint(),
                  EndpointAvailability::Online);
  Pump(stand,
       [&] {
         for (auto const& text : written) {
           if (!HasLine(receiver.Doc(), text)) {
             return false;
           }
         }
         return true;
       },
       120);

  // Same SharedNode instance, no new connection, no repeated snapshot.
  CHECK(static_cast<void const*>(&*receiver.Doc()) == receiver_doc_before);
  CHECK(sender.counts.node_state == snapshots_before);
  CHECK(receiver.imports <= 1);
  ExpectSameDialog(stand);
}

void TestOfflineWithoutUnload() {
  Stand stand;
  Connect(stand);

  RunOfflineWithoutUnload(stand, stand.host, stand.client, "host");
  ExpectQuiet(stand);
  RunOfflineWithoutUnload(stand, stand.client, stand.host, "client");
  ExpectQuiet(stand);

  // Repeating an Online notification whose observed value did not change must
  // not push a single extra frame onto the wire.
  auto const sends = TotalSends(stand);
  for (int i = 0; i < 5; ++i) {
    stand.host.counting->RepeatAvailabilityNotification(kClient);
    stand.client.counting->RepeatAvailabilityNotification(kHost);
    SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
    SetAvailability(stand, kClient, kHost, EndpointAvailability::Online);
    Advance(stand.host, kShareOfferRetryIntervalUs + 1);
    Advance(stand.client, kShareOfferRetryIntervalUs + 2);
  }
  CHECK(TotalSends(stand) == sends);
  CHECK(!Queued(stand));

  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  ExpectParentsIndependent(stand);
  Report("scenario 3 offline without unload", stand);
}

// ---------------------------------------------------------------------------
// Scenario 4: unload and restore
// ---------------------------------------------------------------------------

void RunUnloadAndRestore(Stand& stand, Replica& gone, Replica& staying,
                         std::string const& prefix) {
  auto const doc_id = gone.doc_id;
  auto const lines_before = gone.Doc()->lines.size();
  auto const snapshots_before = staying.counts.node_state;
  auto const imports_before = gone.imports;

  gone.SaveTree();
  // Full unload: runtime and Domain are destroyed, every object reference is
  // released, only the in-memory storage survives.
  gone.Stop();
  staying.sync->Service(++staying.clock);

  SetAvailability(stand, staying.endpoint(), gone.endpoint(),
                  EndpointAvailability::Offline);
  std::vector<std::string> written;
  for (int i = 0; i < 3; ++i) {
    written.push_back(
        Write(stand, staying, prefix + "-while-down-" + std::to_string(i)));
  }
  auto const sends_before = staying.counts.total;
  for (int i = 0; i < 20; ++i) {
    Advance(staying, kShareOfferRetryIntervalUs * 2 + 1);
  }
  CHECK(staying.counts.total == sends_before);

  gone.Start();
  // The only path back to the child is the parent's persisted reference.
  CHECK(!gone.doc_id.is_valid());
  CHECK(gone.CachedSequence() == 0);
  auto const restored_id = gone.RestoreFromStorage();
  CHECK(restored_id == doc_id);
  CHECK(gone.Doc()->lines.size() == lines_before);
  CHECK(gone.imports == imports_before);

  SetAvailability(stand, staying.endpoint(), gone.endpoint(),
                  EndpointAvailability::Online);
  Pump(stand,
       [&] {
         for (auto const& text : written) {
           if (!HasLine(gone.Doc(), text)) {
             return false;
           }
         }
         return true;
       },
       160);

  // Same dialog, no re-import, no duplicated events.
  CHECK(gone.imports == imports_before);
  CHECK(staying.counts.node_state == snapshots_before);
  CHECK(gone.Doc().id() == doc_id);
  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);
}

void TestUnloadAndRestore() {
  Stand stand;
  Connect(stand);

  RunUnloadAndRestore(stand, stand.client, stand.host, "host");
  ExpectQuiet(stand);
  // Both sides keep writing after the client came back.
  Write(stand, stand.client, "after-client-restore");
  Pump(stand,
       [&] { return HasLine(stand.host.Doc(), "after-client-restore"); }, 60);

  RunUnloadAndRestore(stand, stand.host, stand.client, "client");
  ExpectQuiet(stand);
  Write(stand, stand.host, "after-host-restore");
  Pump(stand,
       [&] { return HasLine(stand.client.Doc(), "after-host-restore"); }, 60);

  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.client.counts.node_state == 0);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  ExpectQuiet(stand);
  Report("scenario 4 unload and restore", stand);
}

// ---------------------------------------------------------------------------
// Scenario 4b: the next origin sequence comes back from storage
// ---------------------------------------------------------------------------

std::uint64_t HighestOriginSequence(SharedDoc const& doc,
                                    std::string const& origin) {
  std::uint64_t highest = 0;
  for (auto const& seen : Observe(doc)) {
    if (seen.id.origin_uid == origin) {
      highest = std::max(highest, seen.id.origin_sequence);
    }
  }
  return highest;
}

std::set<std::pair<std::string, std::uint64_t>> IdentitiesOf(
    SharedDoc const& doc) {
  std::set<std::pair<std::string, std::uint64_t>> ids;
  for (auto const& seen : Observe(doc)) {
    ids.insert({seen.id.origin_uid, seen.id.origin_sequence});
  }
  return ids;
}

// A restarted replica knows nothing until it loads. The origin sequence of its
// next shared Event must come back from the persisted journal, not from a
// value that outlived the runtime, and it must not collide with an identity
// already in that journal.
void RunSequenceRecovery(Stand& stand, Replica& gone, Replica& peer,
                         std::string const& prefix) {
  auto const doc_id = gone.doc_id;
  auto const journal_before = Observe(*gone.Doc()).size();
  auto const identities_before = IdentitiesOf(*gone.Doc());
  auto const highest_before =
      HighestOriginSequence(*gone.Doc(), gone.endpoint());
  CHECK(highest_before != 0);
  CHECK(gone.CachedSequence() == highest_before);

  gone.SaveTree();
  gone.Stop();
  gone.Start();

  // Nothing but the storage survived.
  CHECK(gone.CachedSequence() == 0);
  CHECK(!gone.doc_id.is_valid());
  CHECK(gone.sync->RegisteredNodeIds().empty());

  // Recovery starts at the ordinary parent Node and reaches the child only
  // through its persisted reference.
  CHECK(gone.RestoreFromStorage() == doc_id);
  CHECK(Observe(*gone.Doc()).size() == journal_before);

  auto const text = Write(stand, gone, prefix + "-after-unload");
  CHECK(gone.CachedSequence() == highest_before + 1);
  auto const after = Observe(*gone.Doc());
  CHECK(after.size() == journal_before + 1);
  auto const& fresh = after.back();
  CHECK(fresh.text == text);
  CHECK(fresh.id.origin_uid == gone.endpoint());
  CHECK(fresh.id.origin_sequence == highest_before + 1);
  CHECK(identities_before.count(
            {fresh.id.origin_uid, fresh.id.origin_sequence}) == 0);

  Pump(stand, [&] { return HasLine(peer.Doc(), text); }, 80);
  CHECK(Observe(*peer.Doc()).size() == journal_before + 1);
  ExpectSameDialog(stand);
}

void TestSequenceRecoveredFromStorage() {
  Stand stand;
  Connect(stand);

  std::size_t expected = 3;
  for (int i = 0; i < 2; ++i) {
    Write(stand, stand.host, "seq-host-" + std::to_string(i));
    Write(stand, stand.client, "seq-client-" + std::to_string(i));
    expected += 2;
  }
  Pump(stand,
       [&] {
         return Observe(*stand.host.Doc()).size() == expected &&
                Observe(*stand.client.Doc()).size() == expected;
       },
       120);
  ExpectQuiet(stand);

  RunSequenceRecovery(stand, stand.client, stand.host, "client");
  ExpectQuiet(stand);
  RunSequenceRecovery(stand, stand.host, stand.client, "host");
  ExpectQuiet(stand);

  ExpectParentsIndependent(stand);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.client.counts.node_state == 0);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  Report("scenario 4b sequence recovery", stand);
}

// ---------------------------------------------------------------------------
// Scenario 5: unacknowledged delivery
// ---------------------------------------------------------------------------

void TestUnacknowledgedDelivery() {
  Stand stand;
  Connect(stand);
  ExpectQuiet(stand);

  auto const doc_id = stand.host.doc_id;
  auto const events_before = stand.host.counts.event;
  auto const text = Write(stand, stand.host, "needs-ack");

  Advance(stand.host, 1);
  CHECK(stand.host.counts.event == events_before + 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame first;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), first));
  auto const packet_id = first.packet_id;
  auto const identity = first.identity;

  // The event packet is lost.
  CHECK(stand.network.DropNext(kHost, kClient));

  // Offline suppresses the retries of a pending packet as well.
  SetAvailability(stand, kHost, kClient, EndpointAvailability::Offline);
  auto const sends_offline = stand.host.counts.total;
  for (int i = 0; i < 15; ++i) {
    Advance(stand.host, kShareOfferRetryIntervalUs * 2 + 1);
  }
  CHECK(stand.host.counts.total == sends_offline);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.network.PendingCount(kHost, kClient) == 0);

  // Unknown is not Online, and it must not block the pending packet forever.
  SetAvailability(stand, kHost, kClient, EndpointAvailability::Unknown);
  CHECK(stand.network.Availability(kHost, kClient) ==
        EndpointAvailability::Unknown);
  auto const retries_before = stand.host.counts.retries;
  Advance(stand.host, kShareOfferRetryIntervalUs + 1);
  CHECK(stand.host.counts.total > sends_offline);
  CHECK(stand.host.counts.retries == retries_before + 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame retried;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), retried));
  // The retry is the same frozen packet, not a newly minted one.
  CHECK(retried.packet_id == packet_id);
  CHECK(retried.identity == identity);

  // Retries are bounded by the retry interval: below it nothing is re-sent,
  // and one elapsed interval buys exactly one more attempt.
  CHECK(stand.network.DropNext(kHost, kClient));
  auto const paced = stand.host.counts.total;
  for (int i = 0; i < 4; ++i) {
    Advance(stand.host, kShareOfferRetryIntervalUs / 8);
  }
  CHECK(stand.host.counts.total == paced);
  CHECK(stand.network.PendingCount(kHost, kClient) == 0);
  Advance(stand.host, kShareOfferRetryIntervalUs);
  CHECK(stand.host.counts.total == paced + 1);
  for (int i = 0; i < 4; ++i) {
    Advance(stand.host, 1);
  }
  CHECK(stand.host.counts.total == paced + 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame paced_frame;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), paced_frame));
  CHECK(paced_frame.packet_id == packet_id);

  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(HasLine(stand.client.Doc(), text));
  auto const client_journal = Observe(*stand.client.Doc()).size();

  // Now the ACK is lost on its own.
  CHECK(stand.network.PendingCount(kClient, kHost) == 1);
  CHECK(stand.network.DropNext(kClient, kHost));
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));

  // The sender is unloaded while the packet is still unacknowledged.
  stand.host.SaveTree();
  stand.host.Stop();
  stand.network.ClearQueues();
  stand.host.Start();
  CHECK(!stand.host.doc_id.is_valid());
  CHECK(stand.host.CachedSequence() == 0);
  CHECK(stand.host.RestoreFromStorage() == doc_id);
  auto restored_state = SyncStateForPeer(stand.host.Doc(), kClient);
  CHECK(restored_state->HasPendingEvent());
  CHECK(restored_state->pending_event_packet_id == packet_id);
  restored_state = {};

  Advance(stand.host, 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame after_restart;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient),
                         after_restart));
  CHECK(after_restart.packet_id == packet_id);
  CHECK(after_restart.identity == identity);

  // The receiver recognizes the duplicate: no second apply, just another ACK.
  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(Observe(*stand.client.Doc()).size() == client_journal);
  CHECK(stand.client.Doc()->lines.size() == client_journal);
  CHECK(stand.network.PendingCount(kClient, kHost) == 1);
  CHECK(stand.network.DeliverNext(kClient, kHost));

  auto final_state = SyncStateForPeer(stand.host.Doc(), kClient);
  CHECK(!final_state->HasPendingEvent());
  CHECK(final_state->HasDelivered(identity));
  final_state = {};

  ExpectSameDialog(stand);
  ExpectNoDuplicates(Observe(*stand.client.Doc()));
  ExpectParentsIndependent(stand);
  ExpectQuiet(stand);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  Report("scenario 5 unacknowledged delivery", stand);
}

// ---------------------------------------------------------------------------
// Scenario 5b: availability notifications and the retry interval
// ---------------------------------------------------------------------------

// One Event applied by the receiver, its ACK lost. While the sender waits, a
// repeated Online notification whose observed value did not change must not
// buy an early retry: pacing is the retry interval, not the notification.
void TestRepeatedOnlineDoesNotBypassRetryInterval() {
  Stand stand;
  Connect(stand);
  SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
  SetAvailability(stand, kClient, kHost, EndpointAvailability::Online);
  ExpectQuiet(stand);

  auto const text = Write(stand, stand.host, "ack-lost");
  Advance(stand.host, 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame sent;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), sent));
  auto const packet_id = sent.packet_id;
  auto const identity = sent.identity;

  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(HasLine(stand.client.Doc(), text));
  auto const client_journal = Observe(*stand.client.Doc()).size();
  CHECK(stand.network.PendingCount(kClient, kHost) == 1);
  CHECK(stand.network.DropNext(kClient, kHost));
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->pending_event_packet_id ==
        packet_id);

  // Five notifications, total elapsed time still below one retry interval.
  auto const sends = stand.host.counts.total;
  for (int i = 0; i < 5; ++i) {
    stand.host.counting->RepeatAvailabilityNotification(kClient);
    SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
    Advance(stand.host, kShareOfferRetryIntervalUs / 16);
    CHECK(stand.host.counts.total == sends);
    CHECK(
        SyncStateForPeer(stand.host.Doc(), kClient)->pending_event_packet_id ==
        packet_id);
  }
  CHECK(stand.network.PendingCount(kHost, kClient) == 0);

  // The deadline itself buys exactly one retry of the same frozen packet.
  Advance(stand.host, kShareOfferRetryIntervalUs);
  CHECK(stand.host.counts.total == sends + 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame retried;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), retried));
  CHECK(retried.packet_id == packet_id);
  CHECK(retried.identity == identity);
  for (int i = 0; i < 4; ++i) {
    stand.host.counting->RepeatAvailabilityNotification(kClient);
    Advance(stand.host, 1);
  }
  CHECK(stand.host.counts.total == sends + 1);

  // The duplicate is not re-applied, and the correct ACK finishes delivery.
  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(Observe(*stand.client.Doc()).size() == client_journal);
  CHECK(stand.network.PendingCount(kClient, kHost) == 1);
  CHECK(stand.network.DeliverNext(kClient, kHost));
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));

  // With nothing pending, notifications plus Service stay silent.
  auto const quiet = TotalSends(stand);
  for (int i = 0; i < 5; ++i) {
    stand.host.counting->RepeatAvailabilityNotification(kClient);
    stand.client.counting->RepeatAvailabilityNotification(kHost);
    ServiceBoth(stand, kShareOfferRetryIntervalUs + 1);
  }
  CHECK(TotalSends(stand) == quiet);
  CHECK(!Queued(stand));
  ExpectSameDialog(stand);
  CHECK(stand.host.counts.while_offline == 0);
  Report("scenario 5b repeated online vs retry interval", stand);
}

// A real Offline -> Online transition is allowed to release the waiting
// delivery before the deadline. What follows it is paced again.
void TestOfflineToOnlineResumesAndKeepsPacing() {
  Stand stand;
  Connect(stand);
  SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
  SetAvailability(stand, kClient, kHost, EndpointAvailability::Online);
  ExpectQuiet(stand);

  auto const text = Write(stand, stand.host, "resume-pending");
  Advance(stand.host, 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame sent;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), sent));
  auto const packet_id = sent.packet_id;
  // Lost on the wire: the sender keeps waiting with the packet frozen.
  CHECK(stand.network.DropNext(kHost, kClient));

  auto sends = stand.host.counts.total;
  for (int i = 0; i < 3; ++i) {
    stand.host.counting->RepeatAvailabilityNotification(kClient);
    Advance(stand.host, kShareOfferRetryIntervalUs / 16);
  }
  CHECK(stand.host.counts.total == sends);

  SetAvailability(stand, kHost, kClient, EndpointAvailability::Offline);
  Advance(stand.host, kShareOfferRetryIntervalUs / 16);
  CHECK(stand.host.counts.total == sends);
  CHECK(stand.host.counts.while_offline == 0);

  SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
  Advance(stand.host, 1);
  CHECK(stand.host.counts.total == sends + 1);
  EventFrame resumed;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), resumed));
  CHECK(resumed.packet_id == packet_id);

  // The next deadline is counted from the resumed send, not from the moment
  // the endpoint went Online and not from the deadline that was pending
  // before. The packet is dropped again so the wait is observable.
  CHECK(stand.network.DropNext(kHost, kClient));
  sends = stand.host.counts.total;
  std::uint64_t waited = 0;
  for (int i = 0; i < 15; ++i) {
    stand.host.counting->RepeatAvailabilityNotification(kClient);
    SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
    Advance(stand.host, kShareOfferRetryIntervalUs / 16);
    waited += kShareOfferRetryIntervalUs / 16;
    CHECK(stand.host.counts.total == sends);
  }
  // Just short of one full interval since that send, still nothing.
  CHECK(waited < kShareOfferRetryIntervalUs);
  Advance(stand.host, kShareOfferRetryIntervalUs - waited - 1);
  CHECK(stand.host.counts.total == sends);
  // Crossing it buys exactly one repeat of the same frozen packet.
  Advance(stand.host, 1);
  CHECK(stand.host.counts.total == sends + 1);
  EventFrame paced;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), paced));
  CHECK(paced.packet_id == packet_id);

  Pump(stand, [&] { return HasLine(stand.client.Doc(), text); }, 80);
  ExpectSameDialog(stand);
  ExpectQuiet(stand);
  CHECK(stand.host.counts.while_offline == 0);
  Report("scenario 5c offline to online resume", stand);
}

// ---------------------------------------------------------------------------
// Scenario 6: an ACK is the receiver's applied-and-persisted statement
// ---------------------------------------------------------------------------

// A successful Send does not deliver anything, and neither does an Online
// destination. The sender may only consider an Event delivered once the
// receiver has acknowledged it, and the receiver may only acknowledge an
// Event it has already applied and written to its own storage.
void TestAckMeansAppliedAndPersisted() {
  Stand stand;
  Connect(stand);
  SetAvailability(stand, kHost, kClient, EndpointAvailability::Online);
  SetAvailability(stand, kClient, kHost, EndpointAvailability::Online);
  ExpectQuiet(stand);

  auto const doc_id = stand.host.doc_id;
  auto const text = Write(stand, stand.host, "durable-before-ack");
  Advance(stand.host, 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame sent;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), sent));
  auto const identity = sent.identity;
  auto const key =
      std::pair<std::string, std::uint64_t>{identity.origin_uid,
                                            identity.origin_sequence};

  // The Send call returned and the destination reads Online. The sender still
  // treats the Event as undelivered, and the receiver has nothing.
  CHECK(stand.network.Availability(kHost, kClient) ==
        EndpointAvailability::Online);
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));
  CHECK(!HasLine(stand.client.Doc(), text));
  CHECK(ReadPersisted(stand.client).identities.count(key) == 0);

  // Deliver the Event and hold the ACK in the queue.
  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(stand.network.PendingCount(kClient, kHost) == 1);
  AckFrame ack;
  CHECK(DecodeAckFrame(stand.network.PeekNext(kClient, kHost), ack));
  CHECK(ack.packet_id == sent.packet_id);
  CHECK(ack.target_node_id == doc_id);

  // That ACK exists, so the receiver must already be able to survive a crash
  // with the Event in place: a cold reader of its storage sees the identity,
  // the payload and the materialized line.
  auto const persisted = ReadPersisted(stand.client);
  CHECK(persisted.identities.count(key) == 1);
  CHECK(!persisted.lines.empty());
  CHECK(persisted.lines.back() == text);

  // And the sender is still waiting: an ACK that has not arrived is not one.
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));

  // Unload the receiver with the ACK still in flight, and without any
  // test-side save: only what production persisted before acknowledging can
  // come back.
  stand.client.Stop();
  stand.client.Start();
  CHECK(stand.client.RestoreFromStorage() == doc_id);
  CHECK(HasLine(stand.client.Doc(), text));
  CHECK(stand.client.Doc()->lines.back() == text);

  // The queued ACK now reaches the sender and completes the delivery.
  CHECK(stand.network.DeliverNext(kClient, kHost));
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));

  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);
  ExpectQuiet(stand);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.client.counts.node_state == 0);
  CHECK(stand.client.imports == 1);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  Report("scenario 6 ack means applied and persisted", stand);
}

// The Offline side can also be the one that owes an ACK. It still applies and
// persists what arrives, but must not put a single frame on the wire until it
// is Online again, and the ACK it owes may not be lost meanwhile.
void TestAckHeldWhileAcknowledgerOffline() {
  Stand stand;
  Connect(stand);
  ExpectQuiet(stand);

  SetAvailability(stand, kClient, kHost, EndpointAvailability::Offline);
  auto const text = Write(stand, stand.host, "ack-blocked");
  auto const client_sends = stand.client.counts.total;

  Advance(stand.host, 1);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  EventFrame sent;
  CHECK(DecodeEventFrame(stand.network.PeekNext(kHost, kClient), sent));
  auto const identity = sent.identity;
  CHECK(stand.network.DeliverNext(kHost, kClient));

  // Applied and persisted by the Offline replica.
  CHECK(HasLine(stand.client.Doc(), text));
  auto const applied = Observe(*stand.client.Doc()).size();
  CHECK(ReadPersisted(stand.client).identities.count(
            {identity.origin_uid, identity.origin_sequence}) == 1);

  // Nothing left its endpoint, and the ACK was not dropped on the floor.
  CHECK(stand.client.counts.total == client_sends);
  CHECK(stand.client.counts.while_offline == 0);
  CHECK(stand.network.PendingCount(kClient, kHost) == 0);
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());

  // The sender keeps retrying on its own interval. Each duplicate is
  // recognized rather than applied again, and the owed ACK stays owed once.
  for (int i = 0; i < 6; ++i) {
    Advance(stand.host, kShareOfferRetryIntervalUs + 1);
    while (stand.network.DeliverNext(kHost, kClient)) {
    }
    Advance(stand.client, kShareOfferRetryIntervalUs + 1);
  }
  CHECK(Observe(*stand.client.Doc()).size() == applied);
  CHECK(stand.client.Doc()->lines.size() == applied);
  CHECK(stand.client.counts.total == client_sends);
  CHECK(stand.client.counts.while_offline == 0);
  CHECK(stand.network.PendingCount(kClient, kHost) == 0);
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));

  // Online again: the retained ACK goes out once and completes the delivery.
  SetAvailability(stand, kClient, kHost, EndpointAvailability::Online);
  Advance(stand.client, 1);
  CHECK(stand.client.counts.total == client_sends + 1);
  CHECK(stand.network.PendingCount(kClient, kHost) == 1);
  AckFrame ack;
  CHECK(DecodeAckFrame(stand.network.PeekNext(kClient, kHost), ack));
  CHECK(ack.packet_id == sent.packet_id);
  CHECK(stand.network.DeliverNext(kClient, kHost));
  CHECK(!SyncStateForPeer(stand.host.Doc(), kClient)->HasPendingEvent());
  CHECK(SyncStateForPeer(stand.host.Doc(), kClient)->HasDelivered(identity));

  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);
  ExpectQuiet(stand);
  CHECK(stand.host.counts.node_state == 1);
  CHECK(stand.client.counts.node_state == 0);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  Report("scenario 6b ack held while offline", stand);
}

// ---------------------------------------------------------------------------
// Scenario 7: the initial snapshot under Unknown availability
// ---------------------------------------------------------------------------

// Nothing is known about the peer before the first exchange. Unknown must
// still allow the attempt, its repeats are paced by the retry interval rather
// than by the number of Service calls, and every repeat is the packet frozen
// at the first attempt — not a snapshot rebuilt from the Node as it looks now.
void TestInitialSnapshotUnderUnknownIsPacedAndFrozen() {
  Stand stand;
  stand.host.CreateParent();
  stand.client.CreateParent();
  auto doc = stand.host.CreateDoc();
  auto const doc_id = doc.id();
  Write(stand, stand.host, "frozen-1");
  doc = {};
  stand.client.sync->ExpectInitialNodeFromEndpoint(kHost, SharedDoc::kClassId);

  CHECK(stand.network.Availability(kHost, kClient) ==
        EndpointAvailability::Unknown);
  Advance(stand.host, 1);
  CHECK(stand.host.counts.node_state == 1);
  std::vector<std::uint8_t> const first_bytes =
      stand.network.PeekNext(kHost, kClient);
  NodeStateFrame first;
  CHECK(DecodeNodeStateFrame(first_bytes, first));
  CHECK(first.target_node_id == doc_id);
  std::size_t state_journal = 0;
  {
    auto state = SyncStateForPeer(stand.host.Doc(), kClient);
    CHECK(state->GetInitialSyncPhase() == InitialSyncPhase::Pending);
    CHECK(state->pending_initial_packet_id == first.packet_id);
    state_journal = state->journal.size();
  }

  // Lost. Below the deadline nothing is re-sent, however often Service runs.
  CHECK(stand.network.DropNext(kHost, kClient));
  auto const sends = stand.host.counts.total;
  for (int i = 0; i < 8; ++i) {
    Advance(stand.host, kShareOfferRetryIntervalUs / 16);
  }
  CHECK(stand.host.counts.total == sends);
  CHECK(stand.network.PendingCount(kHost, kClient) == 0);

  // The host keeps writing while the snapshot is unacknowledged. The new
  // Event waits: the relationship has no agreed base state yet.
  Write(stand, stand.host, "frozen-2");
  CHECK(stand.host.counts.total == sends);

  // One elapsed interval buys exactly one repeat, and it is the same bytes.
  Advance(stand.host, kShareOfferRetryIntervalUs);
  CHECK(stand.host.counts.total == sends + 1);
  CHECK(stand.host.counts.node_state == 2);
  CHECK(stand.host.counts.event == 0);
  CHECK(stand.network.PendingCount(kHost, kClient) == 1);
  CHECK(stand.network.PeekNext(kHost, kClient) == first_bytes);
  {
    auto state = SyncStateForPeer(stand.host.Doc(), kClient);
    CHECK(state->pending_initial_packet_id == first.packet_id);
    CHECK(state->journal.size() == state_journal);
  }
  for (int i = 0; i < 4; ++i) {
    Advance(stand.host, kShareOfferRetryIntervalUs / 16);
  }
  CHECK(stand.host.counts.total == sends + 1);

  // The same snapshot arrives twice. The second one imports nothing.
  CHECK(stand.network.DuplicateNext(kHost, kClient));
  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(stand.client.imports == 1);
  CHECK(stand.network.DeliverNext(kHost, kClient));
  CHECK(stand.client.imports == 1);

  Pump(stand,
       [&] {
         return HasLine(stand.client.Doc(), "frozen-1") &&
                HasLine(stand.client.Doc(), "frozen-2");
       },
       80);

  // The snapshot carried the state it was frozen with; the later write
  // arrived as an ordinary incremental Event.
  CHECK(stand.host.counts.node_state == 2);
  CHECK(stand.client.counts.node_state == 0);
  CHECK(stand.host.counts.event == 1);
  CHECK(Observe(*stand.client.Doc()).size() == 2);
  ExpectNoDuplicates(Observe(*stand.client.Doc()));
  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);
  ExpectQuiet(stand);
  CHECK(stand.host.counts.while_offline == 0);
  CHECK(stand.client.counts.while_offline == 0);
  Report("scenario 7 unknown initial snapshot", stand);
}

// Local parent events stay local: they never reach the peer journal and never
// perturb the shared child.
void TestLocalParentEventsStayLocal() {
  Stand stand;
  Connect(stand);

  auto const host_local_before = CountLocalEvents(*stand.host.Parent());
  auto const client_local_before = CountLocalEvents(*stand.client.Parent());
  auto const shared_before = Observe(*stand.host.Doc()).size();

  for (int i = 0; i < 3; ++i) {
    auto parent = stand.host.Parent();
    auto event = SetParentNoteEvent::ptr::Create(ae::CreateWith{*stand.host.domain});
    event->note = kHostNote;
    parent->Commit(event);
    parent.Save();
  }
  auto const sends_before = TotalSends(stand);
  Settle(stand);
  CHECK(TotalSends(stand) == sends_before);

  CHECK(CountLocalEvents(*stand.host.Parent()) == host_local_before + 3);
  CHECK(CountLocalEvents(*stand.client.Parent()) == client_local_before);
  CHECK(stand.host.Parent()->local_revision == 4);
  CHECK(stand.client.Parent()->local_revision == 1);
  CHECK(Observe(*stand.host.Doc()).size() == shared_before);
  CHECK(Observe(*stand.client.Doc()).size() == shared_before);
  ExpectSameDialog(stand);
  ExpectParentsIndependent(stand);
  Report("local parent events", stand);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();

  apptraverse::test::TestFirstConnection();
  apptraverse::test::TestIncrementalSync();
  apptraverse::test::TestOfflineWithoutUnload();
  apptraverse::test::TestUnloadAndRestore();
  apptraverse::test::TestSequenceRecoveredFromStorage();
  apptraverse::test::TestUnacknowledgedDelivery();
  apptraverse::test::TestRepeatedOnlineDoesNotBypassRetryInterval();
  apptraverse::test::TestOfflineToOnlineResumesAndKeepsPacing();
  apptraverse::test::TestAckMeansAppliedAndPersisted();
  apptraverse::test::TestAckHeldWhileAcknowledgerOffline();
  apptraverse::test::TestInitialSnapshotUnderUnknownIsPacedAndFrozen();
  apptraverse::test::TestLocalParentEventsStayLocal();

  std::cout << "shared_child_node_stand_test OK\n";
  return 0;
}
