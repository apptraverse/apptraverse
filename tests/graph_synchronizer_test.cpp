#include <chrono>
#include <cstdlib>
#include <iostream>
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

class SyncNode;

class AppendNameEvent
    : public apptraverse::EventFor<SyncNode, AppendNameEvent> {
  AE_OBJECT(AppendNameEvent, Event, 0)

 protected:
  AppendNameEvent() = default;

 public:
  explicit AppendNameEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class SyncNode : public apptraverse::NodeFor<SyncNode> {
  AE_OBJECT(SyncNode, Node, 0)

 protected:
  SyncNode() = default;

 public:
  explicit SyncNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(AppendNameEvent const& event) { name += event.suffix; }

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, std::move(recipients));
  }
};

class LoopbackJournalMessageTransport final
    : public apptraverse::IJournalMessageTransport {
 public:
  LoopbackJournalMessageTransport(
      ae::Domain& message_domain, ae::RamDomainStorage& transfer_storage,
      ae::Domain& receiver_domain, ae::RamDomainStorage& receiver_storage,
      apptraverse::JournalMessageReceiver& receiver, SyncNode::ptr& sender_node,
      ae::ObjId receiver_peer)
      : message_domain_{&message_domain},
        transfer_storage_{&transfer_storage},
        receiver_domain_{&receiver_domain},
        receiver_storage_{&receiver_storage},
        receiver_{&receiver},
        sender_node_{&sender_node},
        receiver_peer_{receiver_peer} {}

  std::size_t send_count{0};
  std::vector<ae::ObjId> message_ids;

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
    assert(event_message->target.id().id() == 100);
    assert(transfer_storage_->state.find(ae::ObjId{100}) ==
           transfer_storage_->state.end());
    assert(transfer_storage_->state.find(ae::ObjId{1000}) ==
           transfer_storage_->state.end());

    auto const event_id = event_message->event.id();
    auto& sender = **sender_node_;
    bool found_pending = false;
    for (auto const& record : sender.journal) {
      if (record.event.id() == event_id) {
        auto const* recipient_state = record.FindRecipient(receiver_peer_);
        assert(recipient_state != nullptr);
        assert(recipient_state->delivery_status ==
               apptraverse::DeliveryStatus::kPending);
        found_pending = true;
        break;
      }
    }
    assert(found_pending);

    message_ids.push_back(message.id());
    message.Save();

    receiver_storage_->state.insert(transfer_storage_->state.begin(),
                                    transfer_storage_->state.end());

    apptraverse::JournalTransportMessage::ptr incoming =
        apptraverse::JournalTransportMessage::ptr::Declare(
            ae::CreateWith{*receiver_domain_}.with_id(message.id()));
    incoming.Load();
    receiver_->Receive(std::move(incoming));

    ++send_count;
  }

 private:
  ae::Domain* message_domain_;
  ae::RamDomainStorage* transfer_storage_;
  ae::Domain* receiver_domain_;
  ae::RamDomainStorage* receiver_storage_;
  apptraverse::JournalMessageReceiver* receiver_;
  SyncNode::ptr* sender_node_;
  ae::ObjId receiver_peer_;
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

static_assert(
    !std::is_base_of_v<ae::Obj, apptraverse::IJournalMessageTransport>);
static_assert(!std::is_base_of_v<ae::Obj, apptraverse::GraphSynchronizer>);

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::EventRecordOrigin;
  using apptraverse::GraphJournalScanner;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::AppendNameEvent;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::SyncNode;

  ae::ObjId const receiver_peer{9001};

  ae::RamDomainStorage sender_storage;
  ae::RamDomainStorage receiver_storage;
  ae::RamDomainStorage transfer_storage;

  ae::Domain sender_domain{ae::Now(), sender_storage};
  ae::Domain receiver_domain{ae::Now(), receiver_storage};
  ae::Domain message_domain{ae::Now(), transfer_storage};

  SyncNode::ptr sender_base =
      SyncNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(1000));
  CHECK(static_cast<bool>(sender_base));
  sender_base->name = "Uninitialized sender base";

  SyncNode::ptr sender_node =
      SyncNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(100));
  CHECK(static_cast<bool>(sender_node));
  sender_node->name = "Alice";
  sender_node->base = sender_base;
  sender_node->CaptureBaseStateForTest();

  SyncNode::ptr receiver_base =
      SyncNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(1000));
  CHECK(static_cast<bool>(receiver_base));
  receiver_base->name = "Uninitialized receiver base";

  SyncNode::ptr receiver_node =
      SyncNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(100));
  CHECK(static_cast<bool>(receiver_node));
  receiver_node->name = "Alice";
  receiver_node->base = receiver_base;
  receiver_node->CaptureBaseStateForTest();

  CHECK(sender_node.id().id() == 100);
  CHECK(receiver_node.id().id() == 100);
  CHECK(sender_node.Load().get() != receiver_node.Load().get());
  CHECK(sender_base.id().id() == 1000);
  CHECK(receiver_base.id().id() == 1000);
  CHECK(sender_base.Load().get() != receiver_base.Load().get());
  CHECK(sender_node->journal.empty());
  CHECK(receiver_node->journal.empty());

  AppendNameEvent::ptr event_b =
      AppendNameEvent::ptr::Create(ae::CreateWith{sender_domain}.with_id(200));
  CHECK(static_cast<bool>(event_b));
  event_b->suffix = " B.";
  ae::TimePoint const time_b{std::chrono::microseconds{100}};
  sender_node->CommitEventForTest(event_b, time_b, {receiver_peer});

  AppendNameEvent::ptr event_cooper = AppendNameEvent::ptr::Create(
      ae::CreateWith{sender_domain}.with_id(201));
  CHECK(static_cast<bool>(event_cooper));
  event_cooper->suffix = " Cooper";
  ae::TimePoint const time_cooper{std::chrono::microseconds{200}};
  sender_node->CommitEventForTest(event_cooper, time_cooper, {receiver_peer});

  CHECK(sender_node->name == "Alice B. Cooper");
  CHECK(sender_node->journal.size() == 2);
  CHECK(sender_node->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(sender_node->journal[0].FindRecipient(receiver_peer) != nullptr);
  CHECK(sender_node->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(sender_node->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(sender_node->journal[1].FindRecipient(receiver_peer) != nullptr);
  CHECK(sender_node->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kPending);
  CHECK(receiver_node->name == "Alice");
  CHECK(receiver_node->journal.empty());

  GraphJournalScanner scanner;
  CHECK(CountPending(scanner, sender_node, receiver_peer) == 2);
  CHECK(CountPending(scanner, receiver_node, receiver_peer) == 0);

  JournalMessageReceiver receiver;
  LoopbackJournalMessageTransport transport{
      message_domain, transfer_storage, receiver_domain, receiver_storage,
      receiver, sender_node, receiver_peer};
  GraphSynchronizer synchronizer{receiver_peer, message_domain, transport};

  synchronizer.Synchronize(sender_node);

  CHECK(transport.send_count == 2);
  CHECK(transport.message_ids.size() == 2);
  CHECK(transport.message_ids[0] != transport.message_ids[1]);

  CHECK(sender_node->name == "Alice B. Cooper");
  CHECK(sender_node->journal.size() == 2);
  CHECK(sender_node->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(sender_node->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(sender_node->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(sender_node->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(sender_node->journal[0].event.id().id() == 200);
  CHECK(sender_node->journal[1].event.id().id() == 201);
  CHECK(sender_node->journal[0].time == time_b);
  CHECK(sender_node->journal[1].time == time_cooper);

  CHECK(receiver_node->name == "Alice B. Cooper");
  CHECK(receiver_node->journal.size() == 2);
  CHECK(receiver_node->journal[0].event.id().id() == 200);
  CHECK(receiver_node->journal[0].time == time_b);
  CHECK(receiver_node->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(receiver_node->journal[0].recipients.empty());
  CHECK(receiver_node->journal[1].event.id().id() == 201);
  CHECK(receiver_node->journal[1].time == time_cooper);
  CHECK(receiver_node->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(receiver_node->journal[1].recipients.empty());

  CHECK(receiver_node->journal[0].event.Load().get() !=
        sender_node->journal[0].event.Load().get());
  CHECK(receiver_node->journal[1].event.Load().get() !=
        sender_node->journal[1].event.Load().get());

  auto* receiver_event_b =
      receiver_node->journal[0].event.Load().as<AppendNameEvent>();
  auto* receiver_event_cooper =
      receiver_node->journal[1].event.Load().as<AppendNameEvent>();
  CHECK(receiver_event_b != nullptr);
  CHECK(receiver_event_cooper != nullptr);
  CHECK(receiver_event_b->suffix == " B.");
  CHECK(receiver_event_cooper->suffix == " Cooper");

  CHECK(sender_base->name == "Alice");
  CHECK(receiver_base->name == "Alice");

  CHECK(ContainsObj(transfer_storage, 200));
  CHECK(ContainsObj(transfer_storage, 201));
  CHECK(!ContainsObj(transfer_storage, 100));
  CHECK(!ContainsObj(transfer_storage, 1000));
  CHECK(ContainsObj(transfer_storage, transport.message_ids[0].id()));
  CHECK(ContainsObj(transfer_storage, transport.message_ids[1].id()));
  CHECK(transfer_storage.state.size() == 4);

  CHECK(CountPending(scanner, sender_node, receiver_peer) == 0);
  CHECK(CountPending(scanner, receiver_node, receiver_peer) == 0);

  auto const send_count_after_first = transport.send_count;
  auto const message_ids_after_first = transport.message_ids;
  auto const transfer_size_after_first = transfer_storage.state.size();
  auto const receiver_name_after_first = receiver_node->name;
  auto const receiver_journal_size_after_first = receiver_node->journal.size();
  auto const sender_statuses = std::vector<DeliveryStatus>{
      sender_node->journal[0].FindRecipient(receiver_peer)->delivery_status,
      sender_node->journal[1].FindRecipient(receiver_peer)->delivery_status};

  synchronizer.Synchronize(sender_node);

  CHECK(transport.send_count == send_count_after_first);
  CHECK(transport.message_ids == message_ids_after_first);
  CHECK(transfer_storage.state.size() == transfer_size_after_first);
  CHECK(receiver_node->name == receiver_name_after_first);
  CHECK(receiver_node->journal.size() == receiver_journal_size_after_first);
  CHECK(sender_node->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        sender_statuses[0]);
  CHECK(sender_node->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        sender_statuses[1]);

  sender_node.Save();
  receiver_node.Save();

  ae::Domain sender_reload_domain{ae::Now(), sender_storage};
  ae::Domain receiver_reload_domain{ae::Now(), receiver_storage};

  apptraverse::Node::ptr reloaded_sender = apptraverse::Node::ptr::Declare(
      ae::CreateWith{sender_reload_domain}.with_id(100));
  reloaded_sender.Load();
  apptraverse::Node::ptr reloaded_receiver = apptraverse::Node::ptr::Declare(
      ae::CreateWith{receiver_reload_domain}.with_id(100));
  reloaded_receiver.Load();

  CHECK(reloaded_sender->GetClassId() == SyncNode::kClassId);
  CHECK(reloaded_receiver->GetClassId() == SyncNode::kClassId);

  auto* loaded_sender = reloaded_sender.Load().as<SyncNode>();
  auto* loaded_receiver = reloaded_receiver.Load().as<SyncNode>();
  CHECK(loaded_sender != nullptr);
  CHECK(loaded_receiver != nullptr);
  CHECK(loaded_sender->name == "Alice B. Cooper");
  CHECK(loaded_receiver->name == "Alice B. Cooper");
  CHECK(loaded_sender->journal.size() == 2);
  CHECK(loaded_receiver->journal.size() == 2);

  CHECK(loaded_sender->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(loaded_sender->journal[0].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(loaded_sender->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(loaded_sender->journal[1].FindRecipient(receiver_peer)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(loaded_receiver->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(loaded_receiver->journal[0].recipients.empty());
  CHECK(loaded_receiver->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(loaded_receiver->journal[1].recipients.empty());
  CHECK(loaded_sender->journal[0].time == time_b);
  CHECK(loaded_sender->journal[1].time == time_cooper);
  CHECK(loaded_receiver->journal[0].time == time_b);
  CHECK(loaded_receiver->journal[1].time == time_cooper);
  CHECK(loaded_sender->journal[0].event.id().id() == 200);
  CHECK(loaded_sender->journal[1].event.id().id() == 201);
  CHECK(loaded_receiver->journal[0].event.id().id() == 200);
  CHECK(loaded_receiver->journal[1].event.id().id() == 201);

  auto* loaded_sender_base = loaded_sender->base.Load().as<SyncNode>();
  auto* loaded_receiver_base = loaded_receiver->base.Load().as<SyncNode>();
  CHECK(loaded_sender_base != nullptr);
  CHECK(loaded_receiver_base != nullptr);
  CHECK(loaded_sender_base->name == "Alice");
  CHECK(loaded_receiver_base->name == "Alice");
  CHECK(!loaded_sender_base->base.is_valid());
  CHECK(!loaded_receiver_base->base.is_valid());
  CHECK(loaded_sender_base->journal.empty());
  CHECK(loaded_receiver_base->journal.empty());

  return EXIT_SUCCESS;
}
