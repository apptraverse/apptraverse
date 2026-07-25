#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/event_record.h"
#include "apptraverse/graph_journal_scanner.h"
#include "apptraverse/graph_synchronizer.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/journal_message_transport.h"
#include "apptraverse/journal_transport_message.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class ChildNode;
class RenameChildEvent;

class ChildNode : public apptraverse::NodeFor<ChildNode> {
  AE_OBJECT(ChildNode, Node, 0)

 protected:
  ChildNode() = default;

 public:
  explicit ChildNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(RenameChildEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, std::move(recipients));
  }
};

class RenameChildEvent
    : public apptraverse::EventFor<ChildNode, RenameChildEvent> {
  AE_OBJECT(RenameChildEvent, Event, 0)

 protected:
  RenameChildEvent() = default;

 public:
  explicit RenameChildEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

void ChildNode::Apply(RenameChildEvent const& event) { name = event.name; }

class ParentNode;
class AttachChildEvent;
class RenameParentEvent;

class ParentNode : public apptraverse::NodeFor<ParentNode> {
  AE_OBJECT(ParentNode, Node, 0)

 protected:
  ParentNode() = default;

 public:
  explicit ParentNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(child))

  std::string label;
  ChildNode::ptr child;

  void Apply(AttachChildEvent const& event);
  void Apply(RenameParentEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, std::move(recipients));
  }
};

class AttachChildEvent
    : public apptraverse::EventFor<ParentNode, AttachChildEvent> {
  AE_OBJECT(AttachChildEvent, Event, 0)

 protected:
  AttachChildEvent() = default;

 public:
  explicit AttachChildEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child))

  ChildNode::ptr child;
};

class RenameParentEvent
    : public apptraverse::EventFor<ParentNode, RenameParentEvent> {
  AE_OBJECT(RenameParentEvent, Event, 0)

 protected:
  RenameParentEvent() = default;

 public:
  explicit RenameParentEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

void ParentNode::Apply(AttachChildEvent const& event) { child = event.child; }

void ParentNode::Apply(RenameParentEvent const& event) {
  label = event.label;
}

class ApplicationRoot : public ae::Obj {
  AE_OBJECT(ApplicationRoot, Obj, 0)

 protected:
  ApplicationRoot() = default;

 public:
  explicit ApplicationRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(parent))

  ParentNode::ptr parent;
};

static_assert(!std::is_base_of_v<apptraverse::Node, ApplicationRoot>);

class LoopbackJournalMessageTransport final
    : public apptraverse::IJournalMessageTransport {
 public:
  LoopbackJournalMessageTransport(
      ae::Domain& message_domain, ae::RamDomainStorage& transfer_storage,
      ae::Domain& receiver_domain, ae::RamDomainStorage& receiver_storage,
      apptraverse::JournalMessageReceiver& receiver,
      ApplicationRoot::ptr& sender_root)
      : message_domain_{&message_domain},
        transfer_storage_{&transfer_storage},
        receiver_domain_{&receiver_domain},
        receiver_storage_{&receiver_storage},
        receiver_{&receiver},
        sender_root_{&sender_root} {}

  std::size_t send_count{0};
  std::vector<ae::ObjId> message_ids;
  std::vector<std::pair<ae::ObjId::Type, ae::ObjId::Type>> target_event_pairs;

  void Send(apptraverse::JournalTransportMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    assert(message.domain() == message_domain_);

    auto* event_message =
        message.Load().as<apptraverse::JournalEventMessage>();
    assert(event_message != nullptr);
    assert(event_message->target.is_valid());
    assert(!event_message->target.is_loaded());
    assert(event_message->event.is_valid());
    assert(event_message->event.is_loaded());
    assert(event_message->event->HasValidIdentity());

    apptraverse::EventRecord const* sender_record = nullptr;
    auto const target_id = event_message->target.id().id();
    auto const event_id = event_message->event.id().id();
    auto* sender_parent = (*sender_root_)->parent.Load().get();
    if (sender_parent->obj_id.id() == target_id) {
      for (auto const& record : sender_parent->journal) {
        if (record.event.id().id() == event_id) {
          sender_record = &record;
          break;
        }
      }
    }
    if (sender_record == nullptr && sender_parent->child.is_valid() &&
        sender_parent->child.id().id() == target_id) {
      for (auto const& record : sender_parent->child->journal) {
        if (record.event.id().id() == event_id) {
          sender_record = &record;
          break;
        }
      }
    }
    assert(sender_record != nullptr);
    assert(sender_record->event->sender.id() ==
           event_message->event->sender.id());
    assert(sender_record->event->sequence == event_message->event->sequence);

    message_ids.push_back(message.id());
    target_event_pairs.emplace_back(event_message->target.id().id(),
                                    event_message->event.id().id());

    message.Save();

    receiver_storage_->state.insert(transfer_storage_->state.begin(),
                                    transfer_storage_->state.end());

    apptraverse::JournalTransportMessage::ptr incoming =
        apptraverse::JournalTransportMessage::ptr::Declare(
            ae::CreateWith{*receiver_domain_}.with_id(message.id()));
    incoming.Load();

    auto* incoming_event_message =
        incoming.Load().as<apptraverse::JournalEventMessage>();
    assert(incoming_event_message != nullptr);
    assert(incoming_event_message->event->HasValidIdentity());
    assert(incoming_event_message->event->sender.id() ==
           sender_record->event->sender.id());
    assert(incoming_event_message->event->sequence ==
           sender_record->event->sequence);
    assert(incoming_event_message->event.is_valid());

    receiver_->Receive(incoming);

    assert(incoming_event_message->event.is_loaded());

    ++send_count;
  }

 private:
  ae::Domain* message_domain_;
  ae::RamDomainStorage* transfer_storage_;
  ae::Domain* receiver_domain_;
  ae::RamDomainStorage* receiver_storage_;
  apptraverse::JournalMessageReceiver* receiver_;
  ApplicationRoot::ptr* sender_root_;
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

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId::Type id) {
  return storage.state.find(ae::ObjId{id}) != storage.state.end();
}

int CountPending(apptraverse::GraphJournalScanner const& scanner, auto& root,
                 ae::ObjId recipient) {
  int count = 0;
  scanner.VisitPending(
      root, recipient,
      [&](apptraverse::Node&, apptraverse::EventRecord&,
          apptraverse::EventRecipientState&) { ++count; });
  return count;
}

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphJournalScanner;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::ApplicationRoot;
  using apptraverse::test::AttachChildEvent;
  using apptraverse::test::ChildNode;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::ParentNode;
  using apptraverse::test::RenameChildEvent;
  using apptraverse::test::RenameParentEvent;

  ae::ObjId const receiver_peer{9001};

  ae::RamDomainStorage sender_storage;
  ae::RamDomainStorage receiver_storage;
  ae::RamDomainStorage attach_transfer_storage;
  ae::RamDomainStorage update_transfer_storage;

  ae::Domain sender_domain{ae::Now(), sender_storage};
  ae::Domain receiver_domain{ae::Now(), receiver_storage};
  ae::Domain attach_message_domain{ae::Now(), attach_transfer_storage};
  ae::Domain update_message_domain{ae::Now(), update_transfer_storage};

  ParentNode::ptr sender_parent_base =
      ParentNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(1100));
  CHECK(static_cast<bool>(sender_parent_base));
  sender_parent_base->label = "Uninitialized parent base";
  CHECK(!sender_parent_base->child.is_valid());
  CHECK(!sender_parent_base->base.is_valid());
  CHECK(sender_parent_base->journal.empty());

  ParentNode::ptr sender_parent =
      ParentNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(100));
  CHECK(static_cast<bool>(sender_parent));
  sender_parent->label = "Parent";
  CHECK(!sender_parent->child.is_valid());
  sender_parent->base = sender_parent_base;
  CHECK(sender_parent->journal.empty());
  sender_parent->CaptureBaseStateForTest();

  CHECK(sender_parent->label == "Parent");
  CHECK(sender_parent_base->label == "Parent");
  CHECK(!sender_parent->child.is_valid());
  CHECK(!sender_parent_base->child.is_valid());
  CHECK(sender_parent->journal.empty());
  CHECK(sender_parent_base->journal.empty());

  ParentNode::ptr receiver_parent_base =
      ParentNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(1100));
  CHECK(static_cast<bool>(receiver_parent_base));
  receiver_parent_base->label = "Uninitialized parent base";

  ParentNode::ptr receiver_parent =
      ParentNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(100));
  CHECK(static_cast<bool>(receiver_parent));
  receiver_parent->label = "Parent";
  CHECK(!receiver_parent->child.is_valid());
  receiver_parent->base = receiver_parent_base;
  receiver_parent->CaptureBaseStateForTest();

  CHECK(sender_parent.id().id() == 100);
  CHECK(receiver_parent.id().id() == 100);
  CHECK(sender_parent.Load().get() != receiver_parent.Load().get());
  CHECK(sender_parent_base.id().id() == 1100);
  CHECK(receiver_parent_base.id().id() == 1100);
  CHECK(sender_parent_base.Load().get() != receiver_parent_base.Load().get());
  CHECK(!receiver_parent->child.is_valid());

  ApplicationRoot::ptr sender_root = ApplicationRoot::ptr::Create(
      ae::CreateWith{sender_domain}.with_id(1));
  CHECK(static_cast<bool>(sender_root));
  sender_root->parent = sender_parent;

  ApplicationRoot::ptr receiver_root = ApplicationRoot::ptr::Create(
      ae::CreateWith{receiver_domain}.with_id(1));
  CHECK(static_cast<bool>(receiver_root));
  receiver_root->parent = receiver_parent;

  CHECK(sender_root.id().id() == 1);
  CHECK(receiver_root.id().id() == 1);
  CHECK(sender_root.Load().get() != receiver_root.Load().get());
  CHECK(sender_root.is_loaded());
  CHECK(receiver_root.is_loaded());
  CHECK(sender_root->parent.Load().get() == sender_parent.Load().get());
  CHECK(receiver_root->parent.Load().get() == receiver_parent.Load().get());

  ChildNode::ptr sender_child_base =
      ChildNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(1200));
  CHECK(static_cast<bool>(sender_child_base));
  sender_child_base->name = "Uninitialized child base";

  ChildNode::ptr sender_child =
      ChildNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(200));
  CHECK(static_cast<bool>(sender_child));
  sender_child->name = "Root";
  sender_child->base = sender_child_base;
  CHECK(sender_child->journal.empty());
  sender_child->CaptureBaseStateForTest();

  CHECK(sender_child->name == "Root");
  CHECK(sender_child_base->name == "Root");
  CHECK(sender_child->journal.empty());
  CHECK(sender_child_base->journal.empty());

  CHECK(!receiver_parent->child.is_valid());
  CHECK(!static_cast<bool>(receiver_domain.Find(ae::ObjId{200})));
  CHECK(!ContainsObj(receiver_storage, 200));
  CHECK(!ContainsObj(receiver_storage, 1200));

  AttachChildEvent::ptr attach_event = AttachChildEvent::ptr::Create(
      ae::CreateWith{sender_domain}.with_id(300));
  CHECK(static_cast<bool>(attach_event));
  attach_event->child = sender_child;
  CHECK(attach_event->child.is_valid());
  CHECK(attach_event->child.is_loaded());
  CHECK(attach_event->child.id().id() == 200);

  ae::TimePoint const attach_time{std::chrono::microseconds{100}};
  attach_event->sender = sender_parent;
  sender_parent->CommitEventForTest(attach_event, attach_time, {receiver_peer});

  CHECK(sender_parent->child.is_valid());
  CHECK(sender_parent->child.is_loaded());
  CHECK(sender_parent->child.Load().get() == sender_child.Load().get());
  CHECK(sender_parent->journal.size() == 1);
  CHECK(sender_parent->journal[0].event.id().id() == 300);
  CHECK(sender_parent->journal[0].event->sender.id() == sender_parent.id());
  CHECK(sender_parent->journal[0].event->sequence == 1);
  CHECK(sender_parent->journal[0].FindRecipient(receiver_peer) != nullptr);
  CHECK(sender_parent->journal[0]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kPending);
  CHECK(!receiver_parent->child.is_valid());

  GraphJournalScanner scanner;
  CHECK(CountPending(scanner, sender_root, receiver_peer) == 1);
  CHECK(CountPending(scanner, receiver_root, receiver_peer) == 0);

  JournalMessageReceiver receiver;
  LoopbackJournalMessageTransport attach_transport{
      attach_message_domain, attach_transfer_storage, receiver_domain,
      receiver_storage, receiver, sender_root};
  GraphSynchronizer attach_synchronizer{receiver_peer, attach_message_domain,
                                        attach_transport};

  attach_synchronizer.Synchronize(sender_root);

  CHECK(attach_transport.send_count == 1);
  CHECK(attach_transport.message_ids.size() == 1);
  CHECK(attach_transport.target_event_pairs.size() == 1);
  CHECK(attach_transport.target_event_pairs[0] ==
        std::make_pair(ae::ObjId::Type{100}, ae::ObjId::Type{300}));

  CHECK(sender_parent->journal[0]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(sender_parent->child.Load().get() == sender_child.Load().get());
  CHECK(sender_child->journal.empty());

  CHECK(receiver_parent->label == "Parent");
  CHECK(receiver_parent->child.is_valid());
  CHECK(receiver_parent->child.is_loaded());
  CHECK(receiver_parent->child.id().id() == 200);
  CHECK(receiver_parent->child->name == "Root");
  CHECK(receiver_parent->child.Load().get() != sender_child.Load().get());
  CHECK(receiver_parent->child->base.id().id() == 1200);
  CHECK(receiver_parent->child->base.is_loaded());
  auto* receiver_child_base =
      receiver_parent->child->base.Load().as<ChildNode>();
  CHECK(receiver_child_base != nullptr);
  CHECK(receiver_child_base->name == "Root");
  CHECK(!receiver_child_base->base.is_valid());
  CHECK(receiver_child_base->journal.empty());
  CHECK(receiver_parent->child->journal.empty());

  CHECK(receiver_parent->journal.size() == 1);
  CHECK(receiver_parent->journal[0].event.id().id() == 300);
  CHECK(receiver_parent->journal[0].time == attach_time);
  CHECK(receiver_parent->journal[0].event->sender.id() == sender_parent.id());
  CHECK(receiver_parent->journal[0].event->sequence == 1);
  CHECK(receiver_parent->journal[0].recipients.empty());

  auto* receiver_attach =
      receiver_parent->journal[0].event.Load().as<AttachChildEvent>();
  CHECK(receiver_attach != nullptr);
  CHECK(receiver_attach->child.is_valid());
  CHECK(receiver_attach->child.is_loaded());
  CHECK(receiver_attach->child.id().id() == 200);
  CHECK(receiver_attach->child.Load().get() ==
        receiver_parent->child.Load().get());

  CHECK(attach_transfer_storage.state.size() == 4);
  CHECK(ContainsObj(attach_transfer_storage,
                    attach_transport.message_ids[0].id()));
  CHECK(ContainsObj(attach_transfer_storage, 300));
  CHECK(ContainsObj(attach_transfer_storage, 200));
  CHECK(ContainsObj(attach_transfer_storage, 1200));
  CHECK(!ContainsObj(attach_transfer_storage, 1));
  CHECK(!ContainsObj(attach_transfer_storage, 100));
  CHECK(!ContainsObj(attach_transfer_storage, 1100));

  CHECK(CountPending(scanner, sender_root, receiver_peer) == 0);
  CHECK(CountPending(scanner, receiver_root, receiver_peer) == 0);

  RenameParentEvent::ptr rename_parent = RenameParentEvent::ptr::Create(
      ae::CreateWith{sender_domain}.with_id(301));
  CHECK(static_cast<bool>(rename_parent));
  rename_parent->label = "Parent updated";
  ae::TimePoint const rename_parent_time{std::chrono::microseconds{200}};
  rename_parent->sender = sender_parent;
  sender_parent->CommitEventForTest(rename_parent, rename_parent_time,
                                    {receiver_peer});

  RenameChildEvent::ptr rename_child = RenameChildEvent::ptr::Create(
      ae::CreateWith{sender_domain}.with_id(302));
  CHECK(static_cast<bool>(rename_child));
  rename_child->name = "Root Value";
  ae::TimePoint const rename_child_time{std::chrono::microseconds{100}};
  rename_child->sender = sender_child;
  sender_child->CommitEventForTest(rename_child, rename_child_time,
                                   {receiver_peer});

  CHECK(sender_parent->label == "Parent updated");
  CHECK(sender_child->name == "Root Value");
  CHECK(receiver_parent->label == "Parent");
  CHECK(receiver_parent->child->name == "Root");
  CHECK(CountPending(scanner, sender_root, receiver_peer) == 2);
  CHECK(CountPending(scanner, receiver_root, receiver_peer) == 0);
  CHECK(sender_parent->journal.size() == 2);
  CHECK(sender_parent->journal[1].event.id().id() == 301);
  CHECK(sender_parent->journal[1].event->sender.id() == sender_parent.id());
  CHECK(sender_parent->journal[1].event->sequence == 2);
  CHECK(sender_parent->journal[1]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kPending);
  CHECK(sender_child->journal.size() == 1);
  CHECK(sender_child->journal[0].event.id().id() == 302);
  CHECK(sender_child->journal[0].event->sender.id() == sender_child.id());
  CHECK(sender_child->journal[0].event->sequence == 1);

  LoopbackJournalMessageTransport update_transport{
      update_message_domain, update_transfer_storage, receiver_domain,
      receiver_storage, receiver, sender_root};
  GraphSynchronizer update_synchronizer{receiver_peer, update_message_domain,
                                        update_transport};

  auto* receiver_child_address_before =
      receiver_parent->child.Load().get();
  CHECK(receiver_child_address_before != nullptr);

  update_synchronizer.Synchronize(sender_root);

  CHECK(update_transport.send_count == 2);
  CHECK(update_transport.message_ids.size() == 2);
  CHECK(update_transport.message_ids[0] != update_transport.message_ids[1]);

  std::set<std::pair<ae::ObjId::Type, ae::ObjId::Type>> const expected_pairs{
      {100, 301},
      {200, 302},
  };
  std::set<std::pair<ae::ObjId::Type, ae::ObjId::Type>> const actual_pairs(
      update_transport.target_event_pairs.begin(),
      update_transport.target_event_pairs.end());
  CHECK(actual_pairs == expected_pairs);

  CHECK(sender_parent->label == "Parent updated");
  CHECK(sender_child->name == "Root Value");
  CHECK(sender_parent->journal[1]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(sender_child->journal[0]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kDelivered);

  CHECK(receiver_parent->label == "Parent updated");
  CHECK(receiver_parent->child->name == "Root Value");
  CHECK(receiver_parent->child.Load().get() == receiver_child_address_before);
  CHECK(receiver_parent->journal.size() == 2);
  CHECK(receiver_parent->child->journal.size() == 1);

  CHECK(receiver_parent->journal[0].event.id().id() == 300);
  CHECK(receiver_parent->journal[0].time == attach_time);
  CHECK(receiver_parent->journal[0].recipients.empty());
  CHECK(receiver_parent->journal[1].event.id().id() == 301);
  CHECK(receiver_parent->journal[1].time == rename_parent_time);
  CHECK(receiver_parent->journal[1].event->sender.id() == sender_parent.id());
  CHECK(receiver_parent->journal[1].event->sequence == 2);
  CHECK(receiver_parent->journal[1].recipients.empty());

  CHECK(receiver_parent->child->journal[0].event.id().id() == 302);
  CHECK(receiver_parent->child->journal[0].time == rename_child_time);
  CHECK(receiver_parent->child->journal[0].event->sender.id() ==
        sender_child.id());
  CHECK(receiver_parent->child->journal[0].event->sequence == 1);
  CHECK(receiver_parent->child->journal[0].recipients.empty());

  auto* receiver_rename_parent =
      receiver_parent->journal[1].event.Load().as<RenameParentEvent>();
  auto* receiver_rename_child =
      receiver_parent->child->journal[0].event.Load().as<RenameChildEvent>();
  CHECK(receiver_rename_parent != nullptr);
  CHECK(receiver_rename_child != nullptr);
  CHECK(receiver_rename_parent->label == "Parent updated");
  CHECK(receiver_rename_child->name == "Root Value");
  CHECK(receiver_parent->journal[1].event.Load().get() !=
        sender_parent->journal[1].event.Load().get());
  CHECK(receiver_parent->child->journal[0].event.Load().get() !=
        sender_child->journal[0].event.Load().get());

  CHECK(update_transfer_storage.state.size() == 4);
  CHECK(ContainsObj(update_transfer_storage,
                    update_transport.message_ids[0].id()));
  CHECK(ContainsObj(update_transfer_storage,
                    update_transport.message_ids[1].id()));
  CHECK(ContainsObj(update_transfer_storage, 301));
  CHECK(ContainsObj(update_transfer_storage, 302));
  CHECK(!ContainsObj(update_transfer_storage, 1));
  CHECK(!ContainsObj(update_transfer_storage, 100));
  CHECK(!ContainsObj(update_transfer_storage, 1100));
  CHECK(!ContainsObj(update_transfer_storage, 200));
  CHECK(!ContainsObj(update_transfer_storage, 1200));
  CHECK(!ContainsObj(update_transfer_storage, 300));

  auto const send_count_after = update_transport.send_count;
  auto const message_ids_after = update_transport.message_ids;
  auto const transfer_size_after = update_transfer_storage.state.size();
  auto const sender_parent_label = sender_parent->label;
  auto const sender_child_name = sender_child->name;
  auto const receiver_parent_label = receiver_parent->label;
  auto const receiver_child_name = receiver_parent->child->name;
  auto const sender_parent_journal_size = sender_parent->journal.size();
  auto const sender_child_journal_size = sender_child->journal.size();
  auto const receiver_parent_journal_size = receiver_parent->journal.size();
  auto const receiver_child_journal_size =
      receiver_parent->child->journal.size();

  update_synchronizer.Synchronize(sender_root);

  CHECK(update_transport.send_count == send_count_after);
  CHECK(update_transport.message_ids == message_ids_after);
  CHECK(update_transfer_storage.state.size() == transfer_size_after);
  CHECK(sender_parent->label == sender_parent_label);
  CHECK(sender_child->name == sender_child_name);
  CHECK(receiver_parent->label == receiver_parent_label);
  CHECK(receiver_parent->child->name == receiver_child_name);
  CHECK(sender_parent->journal.size() == sender_parent_journal_size);
  CHECK(sender_child->journal.size() == sender_child_journal_size);
  CHECK(receiver_parent->journal.size() == receiver_parent_journal_size);
  CHECK(receiver_parent->child->journal.size() ==
        receiver_child_journal_size);
  CHECK(CountPending(scanner, sender_root, receiver_peer) == 0);
  CHECK(CountPending(scanner, receiver_root, receiver_peer) == 0);

  CHECK(sender_parent_base->label == "Parent");
  CHECK(!sender_parent_base->child.is_valid());
  CHECK(!sender_parent_base->base.is_valid());
  CHECK(sender_parent_base->journal.empty());
  CHECK(receiver_parent_base->label == "Parent");
  CHECK(!receiver_parent_base->child.is_valid());
  CHECK(!receiver_parent_base->base.is_valid());
  CHECK(receiver_parent_base->journal.empty());

  CHECK(sender_child_base->name == "Root");
  CHECK(!sender_child_base->base.is_valid());
  CHECK(sender_child_base->journal.empty());
  auto* receiver_child_base_after =
      receiver_parent->child->base.Load().as<ChildNode>();
  CHECK(receiver_child_base_after != nullptr);
  CHECK(receiver_child_base_after->name == "Root");
  CHECK(!receiver_child_base_after->base.is_valid());
  CHECK(receiver_child_base_after->journal.empty());

  sender_root.Save();
  receiver_root.Save();

  ae::Domain sender_reload_domain{ae::Now(), sender_storage};
  ae::Domain receiver_reload_domain{ae::Now(), receiver_storage};

  ApplicationRoot::ptr loaded_sender_root = ApplicationRoot::ptr::Declare(
      ae::CreateWith{sender_reload_domain}.with_id(1));
  loaded_sender_root.Load();
  ApplicationRoot::ptr loaded_receiver_root = ApplicationRoot::ptr::Declare(
      ae::CreateWith{receiver_reload_domain}.with_id(1));
  loaded_receiver_root.Load();

  CHECK(loaded_sender_root.is_loaded());
  CHECK(loaded_receiver_root.is_loaded());
  CHECK(loaded_sender_root->GetClassId() == ApplicationRoot::kClassId);
  CHECK(loaded_receiver_root->GetClassId() == ApplicationRoot::kClassId);

  auto* loaded_sender_parent =
      loaded_sender_root->parent.Load().as<ParentNode>();
  auto* loaded_receiver_parent =
      loaded_receiver_root->parent.Load().as<ParentNode>();
  CHECK(loaded_sender_parent != nullptr);
  CHECK(loaded_receiver_parent != nullptr);
  CHECK(loaded_sender_parent->obj_id.id() == 100);
  CHECK(loaded_receiver_parent->obj_id.id() == 100);
  CHECK(loaded_sender_parent->label == "Parent updated");
  CHECK(loaded_receiver_parent->label == "Parent updated");

  auto* loaded_sender_child = loaded_sender_parent->child.Load().as<ChildNode>();
  auto* loaded_receiver_child =
      loaded_receiver_parent->child.Load().as<ChildNode>();
  CHECK(loaded_sender_child != nullptr);
  CHECK(loaded_receiver_child != nullptr);
  CHECK(loaded_sender_child->obj_id.id() == 200);
  CHECK(loaded_receiver_child->obj_id.id() == 200);
  CHECK(loaded_sender_child->name == "Root Value");
  CHECK(loaded_receiver_child->name == "Root Value");

  CHECK(loaded_sender_parent->journal.size() == 2);
  CHECK(loaded_receiver_parent->journal.size() == 2);
  CHECK(loaded_sender_parent->journal[0].event.id().id() == 300);
  CHECK(loaded_sender_parent->journal[1].event.id().id() == 301);
  CHECK(loaded_sender_parent->journal[0]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(loaded_sender_parent->journal[1]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(loaded_receiver_parent->journal[0].event.id().id() == 300);
  CHECK(loaded_receiver_parent->journal[1].event.id().id() == 301);
  CHECK(loaded_receiver_parent->journal[0].recipients.empty());
  CHECK(loaded_receiver_parent->journal[1].recipients.empty());

  CHECK(loaded_sender_child->journal.size() == 1);
  CHECK(loaded_receiver_child->journal.size() == 1);
  CHECK(loaded_sender_child->journal[0].event.id().id() == 302);
  CHECK(loaded_receiver_child->journal[0].event.id().id() == 302);
  CHECK(loaded_sender_child->journal[0]
            .FindRecipient(receiver_peer)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(loaded_receiver_child->journal[0].recipients.empty());

  auto* loaded_sender_parent_base =
      loaded_sender_parent->base.Load().as<ParentNode>();
  auto* loaded_receiver_parent_base =
      loaded_receiver_parent->base.Load().as<ParentNode>();
  CHECK(loaded_sender_parent_base != nullptr);
  CHECK(loaded_receiver_parent_base != nullptr);
  CHECK(loaded_sender_parent_base->label == "Parent");
  CHECK(loaded_receiver_parent_base->label == "Parent");
  CHECK(!loaded_sender_parent_base->child.is_valid());
  CHECK(!loaded_receiver_parent_base->child.is_valid());
  CHECK(!loaded_sender_parent_base->base.is_valid());
  CHECK(!loaded_receiver_parent_base->base.is_valid());
  CHECK(loaded_sender_parent_base->journal.empty());
  CHECK(loaded_receiver_parent_base->journal.empty());

  auto* loaded_sender_child_base =
      loaded_sender_child->base.Load().as<ChildNode>();
  auto* loaded_receiver_child_base =
      loaded_receiver_child->base.Load().as<ChildNode>();
  CHECK(loaded_sender_child_base != nullptr);
  CHECK(loaded_receiver_child_base != nullptr);
  CHECK(loaded_sender_child_base->name == "Root");
  CHECK(loaded_receiver_child_base->name == "Root");
  CHECK(!loaded_sender_child_base->base.is_valid());
  CHECK(!loaded_receiver_child_base->base.is_valid());
  CHECK(loaded_sender_child_base->journal.empty());
  CHECK(loaded_receiver_child_base->journal.empty());

  auto* loaded_sender_attach =
      loaded_sender_parent->journal[0].event.Load().as<AttachChildEvent>();
  auto* loaded_receiver_attach =
      loaded_receiver_parent->journal[0].event.Load().as<AttachChildEvent>();
  CHECK(loaded_sender_attach != nullptr);
  CHECK(loaded_receiver_attach != nullptr);
  CHECK(loaded_sender_attach->child.Load().get() ==
        loaded_sender_parent->child.Load().get());
  CHECK(loaded_receiver_attach->child.Load().get() ==
        loaded_receiver_parent->child.Load().get());
  CHECK(loaded_sender_parent->child.Load().get() !=
        loaded_receiver_parent->child.Load().get());

  return EXIT_SUCCESS;
}
