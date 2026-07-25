#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
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

class MultiRecipientNode;
class SenderObject;

class AppendValueEvent
    : public apptraverse::EventFor<MultiRecipientNode, AppendValueEvent> {
  AE_OBJECT(AppendValueEvent, Event, 0)

 protected:
  AppendValueEvent() = default;

 public:
  explicit AppendValueEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class SenderObject : public ae::Obj {
  AE_OBJECT(SenderObject, Obj, 0)

 protected:
  SenderObject() = default;

 public:
  explicit SenderObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()
};

class MultiRecipientNode : public apptraverse::NodeFor<MultiRecipientNode> {
  AE_OBJECT(MultiRecipientNode, Node, 0)

 protected:
  MultiRecipientNode() = default;

 public:
  explicit MultiRecipientNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::string value;

  void Apply(AppendValueEvent const& event) { value += event.suffix; }

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
      apptraverse::JournalMessageReceiver& receiver,
      MultiRecipientNode::ptr& sender_node)
      : message_domain_{&message_domain},
        transfer_storage_{&transfer_storage},
        receiver_domain_{&receiver_domain},
        receiver_storage_{&receiver_storage},
        receiver_{&receiver},
        sender_node_{&sender_node} {}

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
    assert(event_message->event->HasValidIdentity());

    apptraverse::EventRecord const* sender_record = nullptr;
    for (auto const& record : (*sender_node_)->journal) {
      if (record.event.id() == event_message->event.id()) {
        sender_record = &record;
        break;
      }
    }
    assert(sender_record != nullptr);
    assert(sender_record->event->sender.id() ==
           event_message->event->sender.id());
    assert(sender_record->event->sequence == event_message->event->sequence);

    message_ids.push_back(message.id());
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
  MultiRecipientNode::ptr* sender_node_;
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

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId id) {
  return storage.state.find(id) != storage.state.end();
}

int CountPending(apptraverse::GraphJournalScanner const& scanner, auto& root,
                 ae::ObjId recipient) {
  int count = 0;
  scanner.VisitPending(root, recipient,
                       [&](apptraverse::Node&, apptraverse::EventRecord&,
                           apptraverse::EventRecipientState&) { ++count; });
  return count;
}

apptraverse::test::MultiRecipientNode::ptr MakeNode(ae::Domain& domain,
                                                    std::string value) {
  using apptraverse::test::MultiRecipientNode;
  auto base =
      MultiRecipientNode::ptr::Create(ae::CreateWith{domain}.with_id(1000));
  auto node =
      MultiRecipientNode::ptr::Create(ae::CreateWith{domain}.with_id(100));
  node->base = base;
  node->value = std::move(value);
  node->CaptureBaseStateForTest();
  return node;
}

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphJournalScanner;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::AppendValueEvent;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::MultiRecipientNode;
  using apptraverse::test::SenderObject;

  ae::ObjId const peer_b{2};
  ae::ObjId const peer_c{3};

  ae::RamDomainStorage sender_storage;
  ae::RamDomainStorage receiver_b_storage;
  ae::RamDomainStorage receiver_c_storage;
  ae::RamDomainStorage transfer_b_storage;
  ae::RamDomainStorage transfer_c_storage;

  ae::Domain sender_domain{ae::Now(), sender_storage};
  ae::Domain receiver_b_domain{ae::Now(), receiver_b_storage};
  ae::Domain receiver_c_domain{ae::Now(), receiver_c_storage};
  ae::Domain message_domain_b{ae::Now(), transfer_b_storage};
  ae::Domain message_domain_c{ae::Now(), transfer_c_storage};

  SenderObject::ptr sender_object =
      SenderObject::ptr::Create(ae::CreateWith{sender_domain}.with_id(9001));
  CHECK(static_cast<bool>(sender_object));

  auto sender = MakeNode(sender_domain, "A");
  auto receiver_b = MakeNode(receiver_b_domain, "A");
  auto receiver_c = MakeNode(receiver_c_domain, "A");

  CHECK(sender.id().id() == 100);
  CHECK(receiver_b.id().id() == 100);
  CHECK(receiver_c.id().id() == 100);
  CHECK(sender.Load().get() != receiver_b.Load().get());
  CHECK(sender.Load().get() != receiver_c.Load().get());
  CHECK(receiver_b.Load().get() != receiver_c.Load().get());
  CHECK(sender->value == "A");
  CHECK(receiver_b->value == "A");
  CHECK(receiver_c->value == "A");

  AppendValueEvent::ptr event =
      AppendValueEvent::ptr::Create(ae::CreateWith{sender_domain}.with_id(200));
  CHECK(static_cast<bool>(event));
  event->suffix = "X";
  ae::TimePoint const event_time{std::chrono::microseconds{100}};
  event->sender = sender_object;
  sender->CommitEventForTest(event, event_time, {peer_c, peer_b});

  CHECK(sender->value == "AX");
  CHECK(sender->journal.size() == 1);
  CHECK(sender->journal[0].event->sender.id() == sender_object.id());
  CHECK(!sender->journal[0].event->sender.is_loaded());
  CHECK(sender->journal[0].event->sequence == 1);
  CHECK(sender->next_local_sequence == 2);
  CHECK(sender->journal[0].recipients.size() == 2);
  CHECK(sender->journal[0].recipients[0].recipient == peer_b);
  CHECK(sender->journal[0].recipients[1].recipient == peer_c);
  CHECK(sender->journal[0].recipients[0].delivery_status ==
        DeliveryStatus::kPending);
  CHECK(sender->journal[0].recipients[1].delivery_status ==
        DeliveryStatus::kPending);

  JournalMessageReceiver receiver_b_handler;
  JournalMessageReceiver receiver_c_handler;
  LoopbackJournalMessageTransport transport_b{
      message_domain_b, transfer_b_storage, receiver_b_domain,
      receiver_b_storage, receiver_b_handler, sender};
  LoopbackJournalMessageTransport transport_c{
      message_domain_c, transfer_c_storage, receiver_c_domain,
      receiver_c_storage, receiver_c_handler, sender};

  GraphJournalScanner scanner;
  CHECK(CountPending(scanner, sender, peer_b) == 1);
  CHECK(CountPending(scanner, sender, peer_c) == 1);

  GraphSynchronizer sync_b{peer_b, message_domain_b, transport_b};
  sync_b.Synchronize(sender);

  CHECK(transport_b.send_count == 1);
  CHECK(transfer_b_storage.state.size() == 2);
  CHECK(ContainsObj(transfer_b_storage, transport_b.message_ids[0]));
  CHECK(ContainsObj(transfer_b_storage, 200));
  CHECK(!ContainsObj(transfer_b_storage, 100));
  CHECK(!ContainsObj(transfer_b_storage, 1000));

  CHECK(receiver_b->value == "AX");
  CHECK(receiver_b->journal.size() == 1);
  CHECK(receiver_b->journal[0].event->sender.id() == sender_object.id());
  CHECK(receiver_b->journal[0].event->sequence == 1);
  CHECK(receiver_b->journal[0].recipients.empty());
  CHECK(receiver_b->journal[0].event.id().id() == 200);

  auto* state_b = sender->journal[0].FindRecipient(peer_b);
  auto* state_c = sender->journal[0].FindRecipient(peer_c);
  CHECK(state_b != nullptr);
  CHECK(state_c != nullptr);
  CHECK(state_b->delivery_status == DeliveryStatus::kDelivered);
  CHECK(state_c->delivery_status == DeliveryStatus::kPending);
  CHECK(CountPending(scanner, sender, peer_b) == 0);
  CHECK(CountPending(scanner, sender, peer_c) == 1);
  CHECK(receiver_c->value == "A");
  CHECK(receiver_c->journal.empty());

  auto const send_count_b_before_repeat = transport_b.send_count;
  sync_b.Synchronize(sender);
  CHECK(transport_b.send_count == send_count_b_before_repeat);
  CHECK(state_c->delivery_status == DeliveryStatus::kPending);
  CHECK(receiver_c->journal.empty());

  GraphSynchronizer sync_c{peer_c, message_domain_c, transport_c};
  sync_c.Synchronize(sender);

  CHECK(transport_c.send_count == 1);
  CHECK(transfer_c_storage.state.size() == 2);
  CHECK(ContainsObj(transfer_c_storage, transport_c.message_ids[0]));
  CHECK(ContainsObj(transfer_c_storage, 200));
  CHECK(!ContainsObj(transfer_c_storage, 100));
  CHECK(!ContainsObj(transfer_c_storage, 1000));

  CHECK(receiver_c->value == "AX");
  CHECK(receiver_c->journal.size() == 1);
  CHECK(receiver_c->journal[0].event->sender.id() == sender_object.id());
  CHECK(receiver_c->journal[0].event->sequence == 1);
  CHECK(receiver_c->journal[0].recipients.empty());
  CHECK(state_b->delivery_status == DeliveryStatus::kDelivered);
  CHECK(state_c->delivery_status == DeliveryStatus::kDelivered);
  CHECK(CountPending(scanner, sender, peer_b) == 0);
  CHECK(CountPending(scanner, sender, peer_c) == 0);

  CHECK(CountPending(scanner, receiver_b, peer_b) == 0);
  CHECK(CountPending(scanner, receiver_b, peer_c) == 0);
  CHECK(CountPending(scanner, receiver_c, peer_b) == 0);
  CHECK(CountPending(scanner, receiver_c, peer_c) == 0);

  ae::RamDomainStorage forward_transfer_storage;
  ae::Domain forward_message_domain{ae::Now(), forward_transfer_storage};
  JournalMessageReceiver forward_receiver;
  LoopbackJournalMessageTransport forward_transport{
      forward_message_domain, forward_transfer_storage, receiver_c_domain,
      receiver_c_storage, forward_receiver, receiver_b};
  GraphSynchronizer receiver_b_to_c{peer_c, forward_message_domain,
                                    forward_transport};
  auto const receiver_c_journal_before = receiver_c->journal.size();
  auto const receiver_c_value_before = receiver_c->value;
  receiver_b_to_c.Synchronize(receiver_b);
  CHECK(forward_transport.send_count == 0);
  CHECK(forward_transfer_storage.state.empty());
  CHECK(receiver_c->journal.size() == receiver_c_journal_before);
  CHECK(receiver_c->value == receiver_c_value_before);

  sender.Save();
  receiver_b.Save();
  receiver_c.Save();

  ae::Domain reload_sender_domain{ae::Now(), sender_storage};
  ae::Domain reload_b_domain{ae::Now(), receiver_b_storage};
  ae::Domain reload_c_domain{ae::Now(), receiver_c_storage};

  MultiRecipientNode::ptr loaded_sender =
      MultiRecipientNode::ptr::Declare(
          ae::CreateWith{reload_sender_domain}.with_id(100));
  loaded_sender.Load();
  MultiRecipientNode::ptr loaded_b = MultiRecipientNode::ptr::Declare(
      ae::CreateWith{reload_b_domain}.with_id(100));
  loaded_b.Load();
  MultiRecipientNode::ptr loaded_c = MultiRecipientNode::ptr::Declare(
      ae::CreateWith{reload_c_domain}.with_id(100));
  loaded_c.Load();

  CHECK(loaded_sender.is_loaded());
  CHECK(loaded_sender->value == "AX");
  CHECK(loaded_sender->journal.size() == 1);
  CHECK(loaded_sender->journal[0].recipients.size() == 2);
  CHECK(loaded_sender->journal[0].recipients[0].recipient == peer_b);
  CHECK(loaded_sender->journal[0].recipients[1].recipient == peer_c);
  CHECK(loaded_sender->journal[0].recipients[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(loaded_sender->journal[0].recipients[1].delivery_status ==
        DeliveryStatus::kDelivered);

  CHECK(loaded_b->value == "AX");
  CHECK(loaded_b->journal.size() == 1);
  CHECK(loaded_b->journal[0].recipients.empty());

  CHECK(loaded_c->value == "AX");
  CHECK(loaded_c->journal.size() == 1);
  CHECK(loaded_c->journal[0].recipients.empty());

  return EXIT_SUCCESS;
}
