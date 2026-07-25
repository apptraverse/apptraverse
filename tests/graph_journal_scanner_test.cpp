#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/event_record.h"
#include "apptraverse/graph_journal_scanner.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class ScanNode;
class BridgeObject;
class GraphRoot;
class RemoteSender;

class AppendNameEvent
    : public apptraverse::EventFor<ScanNode, AppendNameEvent> {
  AE_OBJECT(AppendNameEvent, Event, 0)

 protected:
  AppendNameEvent() = default;

 public:
  explicit AppendNameEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class LinkNodeEvent : public apptraverse::EventFor<ScanNode, LinkNodeEvent> {
  AE_OBJECT(LinkNodeEvent, Event, 0)

 protected:
  LinkNodeEvent() = default;

 public:
  explicit LinkNodeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(linked))

  ae::ObjPtr<ScanNode> linked;
};

class RemoteSender : public ae::Obj {
  AE_OBJECT(RemoteSender, Obj, 0)

 protected:
  RemoteSender() = default;

 public:
  explicit RemoteSender(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()
};

class ScanNode : public apptraverse::NodeFor<ScanNode> {
  AE_OBJECT(ScanNode, Node, 0)

 protected:
  ScanNode() = default;

 public:
  explicit ScanNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, std::move(recipients));
  }

  bool AcceptRemoteEventForTest(apptraverse::Event::ptr event,
                                ae::TimePoint time) {
    return AcceptRemoteEvent(std::move(event), time);
  }

  void Apply(AppendNameEvent const& event) { name += event.suffix; }

  void Apply(LinkNodeEvent const&) {}
};

class BridgeObject : public ae::Obj {
  AE_OBJECT(BridgeObject, Obj, 0)

 protected:
  BridgeObject() = default;

 public:
  explicit BridgeObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(root), AE_MMBR(child))

  ae::ObjPtr<GraphRoot> root;
  ScanNode::ptr child;
};

class GraphRoot : public ae::Obj {
  AE_OBJECT(GraphRoot, Obj, 0)

 protected:
  GraphRoot() = default;

 public:
  explicit GraphRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(direct), AE_MMBR(repeated), AE_MMBR(bridge),
                    AE_MMBR(unloaded))

  ScanNode::ptr direct;
  ScanNode::ptr repeated;
  ae::ObjPtr<BridgeObject> bridge;
  ScanNode::ptr unloaded;
};

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

using PendingKey = std::pair<ae::ObjId::Type, ae::ObjId::Type>;
using apptraverse::test::GraphRoot;
using apptraverse::test::ScanNode;

ScanNode::ptr MakeLiveNode(ae::Domain& domain, ae::ObjId::Type id,
                           ae::ObjId::Type base_id, std::string name) {
  auto node = ScanNode::ptr::Create(ae::CreateWith{domain}.with_id(id));
  auto base = ScanNode::ptr::Create(ae::CreateWith{domain}.with_id(base_id));
  node->base = base;
  node->name = std::move(name);
  node->CaptureBaseStateForTest();
  return node;
}

std::set<PendingKey> CollectPending(apptraverse::GraphJournalScanner const& scanner,
                                    GraphRoot::ptr& root,
                                    ae::ObjId recipient) {
  std::set<PendingKey> found;
  scanner.VisitPending(
      root, recipient,
      [&](apptraverse::Node& node, apptraverse::EventRecord& record,
          apptraverse::EventRecipientState&) {
        found.emplace(node.obj_id.id(), record.event.id().id());
      });
  return found;
}

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphJournalScanner;
  using apptraverse::test::AppendNameEvent;
  using apptraverse::test::BridgeObject;
  using apptraverse::test::GraphRoot;
  using apptraverse::test::LinkNodeEvent;
  using apptraverse::test::RemoteSender;
  using apptraverse::test::ScanNode;

  ae::ObjId const receiver_peer{9001};

  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  RemoteSender::ptr remote_sender =
      RemoteSender::ptr::Create(ae::CreateWith{domain}.with_id(9002));
  CHECK(static_cast<bool>(remote_sender));

  auto node_a = MakeLiveNode(domain, 100, 1100, "A");
  auto node_b = MakeLiveNode(domain, 200, 1200, "B");
  auto node_c = MakeLiveNode(domain, 300, 1300, "C");
  auto node_d = MakeLiveNode(domain, 400, 1400, "D");
  auto node_e = MakeLiveNode(domain, 500, 1500, "E");

  CHECK(node_a->base.is_loaded());
  CHECK(node_a->base->journal.empty());
  CHECK(node_b->base->journal.empty());
  CHECK(node_c->base->journal.empty());
  CHECK(node_d->base->journal.empty());
  CHECK(node_e->base->journal.empty());

  {
    auto link = LinkNodeEvent::ptr::Create(ae::CreateWith{domain}.with_id(1000));
    CHECK(static_cast<bool>(link));
    link->linked = node_d;
    CHECK(link->linked.is_loaded());
    link->sender = node_a;
    node_a->CommitEventForTest(
        link, ae::TimePoint{std::chrono::microseconds{100}}, {receiver_peer});
  }
  {
    auto append =
        AppendNameEvent::ptr::Create(ae::CreateWith{domain}.with_id(1001));
    CHECK(static_cast<bool>(append));
    append->suffix = " local-a";
    append->sender = node_a;
    node_a->CommitEventForTest(
        append, ae::TimePoint{std::chrono::microseconds{200}}, {receiver_peer});
  }
  CHECK(node_a->journal.size() == 2);
  CHECK(node_a->journal[0].event->sender.id() == node_a.id());
  CHECK(node_a->journal[0].event->sequence == 1);
  CHECK(node_a->journal[0].FindRecipient(receiver_peer) != nullptr);
  CHECK(node_a->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_a->journal[1].event->sender.id() == node_a.id());
  CHECK(node_a->journal[1].event->sequence == 2);
  CHECK(node_a->journal[1].FindRecipient(receiver_peer) != nullptr);
  CHECK(node_a->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_a->name == "A local-a");

  {
    auto remote =
        AppendNameEvent::ptr::Create(ae::CreateWith{domain}.with_id(1002));
    CHECK(static_cast<bool>(remote));
    remote->suffix = " remote-b";
    remote->sender = remote_sender;
    remote->sequence = 1;
    CHECK(node_b->AcceptRemoteEventForTest(
        remote, ae::TimePoint{std::chrono::microseconds{100}}));
  }
  {
    auto local =
        AppendNameEvent::ptr::Create(ae::CreateWith{domain}.with_id(1003));
    CHECK(static_cast<bool>(local));
    local->suffix = " local-b";
    local->sender = node_b;
    node_b->CommitEventForTest(
        local, ae::TimePoint{std::chrono::microseconds{200}}, {receiver_peer});
  }
  CHECK(node_b->journal.size() == 2);
  CHECK(node_b->journal[0].event->sender.id() == remote_sender.id());
  CHECK(node_b->journal[0].event->sequence == 1);
  CHECK(node_b->journal[0].recipients.empty());
  CHECK(node_b->journal[1].event->sender.id() == node_b.id());
  CHECK(node_b->journal[1].event->sequence == 1);
  CHECK(node_b->journal[1].FindRecipient(receiver_peer) != nullptr);
  CHECK(node_b->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_b->name == "B remote-b local-b");

  {
    auto append =
        AppendNameEvent::ptr::Create(ae::CreateWith{domain}.with_id(1004));
    CHECK(static_cast<bool>(append));
    append->suffix = " local-c";
    append->sender = node_c;
    node_c->CommitEventForTest(
        append, ae::TimePoint{std::chrono::microseconds{100}}, {receiver_peer});
  }
  CHECK(node_c->journal.size() == 1);
  CHECK(node_c->journal[0].event->sender.id() == node_c.id());
  CHECK(node_c->journal[0].event->sequence == 1);
  CHECK(node_c->journal[0].FindRecipient(receiver_peer) != nullptr);
  CHECK(node_c->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);

  {
    auto append =
        AppendNameEvent::ptr::Create(ae::CreateWith{domain}.with_id(1005));
    CHECK(static_cast<bool>(append));
    append->suffix = " local-d";
    append->sender = node_d;
    node_d->CommitEventForTest(
        append, ae::TimePoint{std::chrono::microseconds{100}}, {receiver_peer});
  }
  CHECK(node_d->journal.size() == 1);
  CHECK(node_d->journal[0].event->sender.id() == node_d.id());
  CHECK(node_d->journal[0].event->sequence == 1);
  CHECK(node_d->journal[0].FindRecipient(receiver_peer) != nullptr);
  CHECK(node_d->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);

  {
    auto append =
        AppendNameEvent::ptr::Create(ae::CreateWith{domain}.with_id(1006));
    CHECK(static_cast<bool>(append));
    append->suffix = " local-e";
    append->sender = node_e;
    node_e->CommitEventForTest(
        append, ae::TimePoint{std::chrono::microseconds{100}}, {receiver_peer});
  }
  CHECK(node_e->journal.size() == 1);
  CHECK(node_e->journal[0].event->sender.id() == node_e.id());
  CHECK(node_e->journal[0].event->sequence == 1);
  CHECK(node_e->journal[0].FindRecipient(receiver_peer) != nullptr);
  CHECK(node_e->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);

  auto root = GraphRoot::ptr::Create(ae::CreateWith{domain}.with_id(1));
  auto bridge = BridgeObject::ptr::Create(ae::CreateWith{domain}.with_id(2));
  CHECK(static_cast<bool>(root));
  CHECK(static_cast<bool>(bridge));

  root->direct = node_a;
  root->repeated = node_a;
  root->bridge = bridge;
  bridge->root = root;
  bridge->child = node_b;

  root->unloaded = node_c;
  root->unloaded.Reset();
  root->unloaded.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  CHECK(root->unloaded.is_valid());
  CHECK(root->unloaded.id().id() == 300);
  CHECK(!root->unloaded.is_loaded());
  CHECK(node_c.is_loaded());

  auto const name_a = node_a->name;
  auto const name_b = node_b->name;
  auto const name_c = node_c->name;
  auto const name_d = node_d->name;
  auto const name_e = node_e->name;

  auto const journal_sizes = std::vector<std::size_t>{
      node_a->journal.size(), node_b->journal.size(), node_c->journal.size(),
      node_d->journal.size(), node_e->journal.size()};
  auto const statuses_a = std::vector<DeliveryStatus>{
      node_a->journal[0].FindRecipient(receiver_peer)->delivery_status,
      node_a->journal[1].FindRecipient(receiver_peer)->delivery_status};
  auto const recipients_b0_empty = node_b->journal[0].recipients.empty();
  auto const status_b1 =
      node_b->journal[1].FindRecipient(receiver_peer)->delivery_status;
  auto const status_c =
      node_c->journal[0].FindRecipient(receiver_peer)->delivery_status;
  auto const status_d =
      node_d->journal[0].FindRecipient(receiver_peer)->delivery_status;
  auto const status_e =
      node_e->journal[0].FindRecipient(receiver_peer)->delivery_status;

  GraphJournalScanner scanner;

  std::set<PendingKey> found;
  std::map<PendingKey, int> counts;
  scanner.VisitPending(
      root, receiver_peer,
      [&](apptraverse::Node& node, apptraverse::EventRecord& record,
          apptraverse::EventRecipientState&) {
        PendingKey key{node.obj_id.id(), record.event.id().id()};
        found.insert(key);
        ++counts[key];
      });

  std::set<PendingKey> const expected{
      {100, 1000},
      {100, 1001},
      {200, 1003},
      {400, 1005},
  };

  CHECK(found == expected);
  CHECK(found.size() == 4);
  CHECK(found.count({100, 1000}) == 1);
  CHECK(found.count({100, 1001}) == 1);
  CHECK(found.count({200, 1003}) == 1);
  CHECK(found.count({400, 1005}) == 1);
  CHECK(found.count({200, 1002}) == 0);
  CHECK(found.count({300, 1004}) == 0);
  CHECK(found.count({500, 1006}) == 0);

  for (auto const& [key, count] : counts) {
    CHECK(count == 1);
  }

  CHECK(node_a->journal.size() == journal_sizes[0]);
  CHECK(node_b->journal.size() == journal_sizes[1]);
  CHECK(node_c->journal.size() == journal_sizes[2]);
  CHECK(node_d->journal.size() == journal_sizes[3]);
  CHECK(node_e->journal.size() == journal_sizes[4]);

  CHECK(node_a->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        statuses_a[0]);
  CHECK(node_a->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        statuses_a[1]);
  CHECK(node_b->journal[0].recipients.empty() == recipients_b0_empty);
  CHECK(node_b->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        status_b1);
  CHECK(node_c->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        status_c);
  CHECK(node_d->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        status_d);
  CHECK(node_e->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        status_e);

  CHECK(node_a->name == name_a);
  CHECK(node_b->name == name_b);
  CHECK(node_c->name == name_c);
  CHECK(node_d->name == name_d);
  CHECK(node_e->name == name_e);

  CHECK(node_a->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_a->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_b->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_d->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(node_b->journal[0].recipients.empty());

  CHECK(!root->unloaded.is_loaded());

  auto second = CollectPending(scanner, root, receiver_peer);
  CHECK(second == found);
  CHECK(second == expected);

  return EXIT_SUCCESS;
}
