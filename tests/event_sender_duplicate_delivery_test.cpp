#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/graph_synchronizer.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/journal_message_transport.h"
#include "apptraverse/journal_transport_message.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class SenderObject : public ae::Obj {
  AE_OBJECT(SenderObject, Obj, 0)

 protected:
  SenderObject() = default;

 public:
  explicit SenderObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()
};

static_assert(!std::is_base_of_v<apptraverse::Node, SenderObject>);

class IdentityNode;

class AppendValueEvent
    : public apptraverse::EventFor<IdentityNode, AppendValueEvent> {
  AE_OBJECT(AppendValueEvent, Event, 0)

 protected:
  AppendValueEvent() = default;

 public:
  explicit AppendValueEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class IdentityNode : public apptraverse::NodeFor<IdentityNode> {
  AE_OBJECT(IdentityNode, Node, 0)

 protected:
  IdentityNode() = default;

 public:
  explicit IdentityNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(apply_count))

  std::string value;
  std::uint32_t apply_count{0};

  void Apply(AppendValueEvent const& event) {
    value += event.suffix;
    ++apply_count;
  }

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, std::move(recipients));
  }
};

class DuplicatingJournalMessageTransport final
    : public apptraverse::IJournalMessageTransport {
 public:
  DuplicatingJournalMessageTransport(
      ae::Domain& message_domain, ae::RamDomainStorage& transfer_storage,
      ae::Domain& receiver_domain, ae::RamDomainStorage& receiver_storage,
      apptraverse::JournalMessageReceiver& receiver)
      : message_domain_{&message_domain},
        transfer_storage_{&transfer_storage},
        receiver_domain_{&receiver_domain},
        receiver_storage_{&receiver_storage},
        receiver_{&receiver} {}

  std::size_t send_count{0};
  std::vector<ae::ObjId> message_ids;
  bool first_event_loaded_before_receive{false};
  bool first_event_loaded_after_receive{false};
  bool duplicate_event_loaded_before_receive{false};
  bool duplicate_event_loaded_after_receive{false};

  void Send(apptraverse::JournalTransportMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    assert(message.domain() == message_domain_);

    auto* event_message =
        message.Load().as<apptraverse::JournalEventMessage>();
    assert(event_message != nullptr);
    assert(event_message->event.is_valid());
    assert(event_message->event.is_loaded());
    assert(event_message->event->HasValidIdentity());
    assert((event_message->event.flags() &
            ae::ObjFlags::kUnloadedByDefault) ==
           ae::ObjFlags::kUnloadedByDefault);

    message.Save();
    message_ids.push_back(message.id());

    auto duplicate = apptraverse::JournalEventMessage::ptr::Create(
        ae::CreateWith{*message_domain_});
    duplicate->target = event_message->target;
    duplicate->event = event_message->event;
    duplicate->time = event_message->time;

    assert(duplicate->event.is_valid());
    assert(duplicate->event.is_loaded());
    assert(duplicate->event.id() == event_message->event.id());
    assert(duplicate.id() != message.id());

    duplicate.Save();
    message_ids.push_back(duplicate.id());

    receiver_storage_->state.insert(transfer_storage_->state.begin(),
                                    transfer_storage_->state.end());

    apptraverse::JournalTransportMessage::ptr first_incoming =
        apptraverse::JournalTransportMessage::ptr::Declare(
            ae::CreateWith{*receiver_domain_}.with_id(message.id()));
    first_incoming.Load();

    auto* first_concrete =
        first_incoming.Load().as<apptraverse::JournalEventMessage>();
    assert(first_concrete != nullptr);
    assert(first_concrete->event.is_valid());
    first_event_loaded_before_receive = first_concrete->event.is_loaded();
    assert(!first_event_loaded_before_receive);

    receiver_->Receive(first_incoming);
    first_event_loaded_after_receive = first_concrete->event.is_loaded();

    apptraverse::JournalTransportMessage::ptr duplicate_incoming =
        apptraverse::JournalTransportMessage::ptr::Declare(
            ae::CreateWith{*receiver_domain_}.with_id(duplicate.id()));
    duplicate_incoming.Load();

    auto* duplicate_concrete =
        duplicate_incoming.Load().as<apptraverse::JournalEventMessage>();
    assert(duplicate_concrete != nullptr);
    assert(duplicate_concrete->event.is_valid());
    duplicate_event_loaded_before_receive =
        duplicate_concrete->event.is_loaded();
    assert(!duplicate_event_loaded_before_receive);

    receiver_->Receive(duplicate_incoming);
    duplicate_event_loaded_after_receive =
        duplicate_concrete->event.is_loaded();

    ++send_count;
  }

 private:
  ae::Domain* message_domain_;
  ae::RamDomainStorage* transfer_storage_;
  ae::Domain* receiver_domain_;
  ae::RamDomainStorage* receiver_storage_;
  apptraverse::JournalMessageReceiver* receiver_;
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

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::AppendValueEvent;
  using apptraverse::test::DuplicatingJournalMessageTransport;
  using apptraverse::test::IdentityNode;
  using apptraverse::test::SenderObject;

  static_assert(std::is_same_v<
                decltype(std::declval<apptraverse::Event&>().sequence),
                std::uint32_t>);

  ae::ObjId const recipient_b{6001};

  ae::RamDomainStorage sender_storage;
  ae::RamDomainStorage receiver_storage;
  ae::RamDomainStorage transfer_storage;

  ae::Domain sender_domain{ae::Now(), sender_storage};
  ae::Domain receiver_domain{ae::Now(), receiver_storage};
  ae::Domain message_domain{ae::Now(), transfer_storage};

  SenderObject::ptr sender_a =
      SenderObject::ptr::Create(ae::CreateWith{sender_domain}.with_id(5001));
  SenderObject::ptr sender_b =
      SenderObject::ptr::Create(ae::CreateWith{sender_domain}.with_id(5002));

  IdentityNode::ptr sender_base =
      IdentityNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(1000));
  sender_base->value = "A";
  sender_base->apply_count = 0;

  IdentityNode::ptr sender =
      IdentityNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(100));
  sender->value = "A";
  sender->apply_count = 0;
  sender->base = sender_base;
  sender->CaptureBaseStateForTest();

  IdentityNode::ptr receiver_base =
      IdentityNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(1000));
  receiver_base->value = "A";
  receiver_base->apply_count = 0;

  IdentityNode::ptr receiver =
      IdentityNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(100));
  receiver->value = "A";
  receiver->apply_count = 0;
  receiver->base = receiver_base;
  receiver->CaptureBaseStateForTest();

  CHECK(sender.Load().get() != receiver.Load().get());
  CHECK(sender->next_local_sequence == 1);

  AppendValueEvent::ptr first_event = AppendValueEvent::ptr::Create(
      ae::CreateWith{sender_domain}.with_id(200));
  first_event->suffix = "X";
  first_event->sender = sender_a;

  ae::TimePoint const first_time{std::chrono::microseconds{100}};
  sender->CommitEventForTest(first_event, first_time, {recipient_b});

  CHECK(first_event->sender.id() == sender_a.id());
  CHECK(!first_event->sender.is_loaded());
  CHECK(first_event->sequence == 1);
  CHECK(sender->next_local_sequence == 2);
  CHECK(sender->value == "AX");
  CHECK(sender->apply_count == 1);
  CHECK(sender->journal.size() == 1);
  CHECK(sender->journal[0].event->sender.id() == sender_a.id());
  CHECK(sender->journal[0].event->sequence == 1);
  CHECK(sender->journal[0].FindRecipient(recipient_b) != nullptr);
  CHECK(sender->journal[0].FindRecipient(recipient_b)->delivery_status ==
        DeliveryStatus::kPending);

  JournalMessageReceiver message_receiver;
  DuplicatingJournalMessageTransport transport{
      message_domain, transfer_storage, receiver_domain, receiver_storage,
      message_receiver};

  GraphSynchronizer synchronizer{recipient_b, message_domain, transport};
  synchronizer.Synchronize(sender);

  CHECK(transport.send_count == 1);
  CHECK(transport.message_ids.size() == 2);
  CHECK(transport.message_ids[0] != transport.message_ids[1]);
  CHECK(!transport.first_event_loaded_before_receive);
  CHECK(transport.first_event_loaded_after_receive);
  CHECK(!transport.duplicate_event_loaded_before_receive);

  CHECK(sender->journal[0].FindRecipient(recipient_b)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(sender->journal.size() == 1);
  CHECK(sender->value == "AX");
  CHECK(sender->apply_count == 1);

  CHECK(receiver->value == "AX");
  CHECK(receiver->apply_count == 1);
  CHECK(receiver->journal.size() == 1);
  CHECK(receiver->journal[0].recipients.empty());
  CHECK(receiver->journal[0].event->sender.id() == sender_a.id());
  CHECK(receiver->journal[0].event->sequence == 1);
  CHECK(receiver->journal[0].event.id().id() == 200);

  CHECK(transfer_storage.state.size() == 3);
  CHECK(ContainsObj(transfer_storage, transport.message_ids[0]));
  CHECK(ContainsObj(transfer_storage, transport.message_ids[1]));
  CHECK(ContainsObj(transfer_storage, 200));
  CHECK(!ContainsObj(transfer_storage, 100));
  CHECK(!ContainsObj(transfer_storage, 1000));

  SenderObject::ptr receiver_sender_a = SenderObject::ptr::Create(
      ae::CreateWith{receiver_domain}.with_id(5001));
  SenderObject::ptr receiver_sender_b = SenderObject::ptr::Create(
      ae::CreateWith{receiver_domain}.with_id(5002));

  AppendValueEvent::ptr second_event = AppendValueEvent::ptr::Create(
      ae::CreateWith{receiver_domain}.with_id(201));
  second_event->suffix = "Y";
  second_event->sender = receiver_sender_a;
  second_event->sender.Reset();
  second_event->sender.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  second_event->sequence = 1;

  apptraverse::Node::ptr generic_receiver = receiver;
  bool const accepted_duplicate = generic_receiver->AcceptRemoteEvent(
      second_event, ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(accepted_duplicate == false);
  CHECK(receiver->value == "AX");
  CHECK(receiver->apply_count == 1);
  CHECK(receiver->journal.size() == 1);

  second_event->sender = receiver_sender_b;
  second_event->sender.Reset();
  second_event->sender.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  second_event->sequence = 1;

  bool const accepted_other_sender = generic_receiver->AcceptRemoteEvent(
      second_event, ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(accepted_other_sender == true);
  CHECK(receiver->value == "AXY");
  CHECK(receiver->apply_count == 2);
  CHECK(receiver->journal.size() == 2);
  CHECK(receiver->journal[1].event->sender.id() == receiver_sender_b.id());
  CHECK(receiver->journal[1].event->sequence == 1);
  CHECK(receiver->journal[1].event.id().id() == 201);

  sender.Save();
  receiver.Save();

  ae::Domain sender_reload_domain{ae::Now(), sender_storage};
  ae::Domain receiver_reload_domain{ae::Now(), receiver_storage};

  IdentityNode::ptr reloaded_sender = IdentityNode::ptr::Declare(
      ae::CreateWith{sender_reload_domain}.with_id(100));
  reloaded_sender.Load();

  IdentityNode::ptr reloaded_receiver = IdentityNode::ptr::Declare(
      ae::CreateWith{receiver_reload_domain}.with_id(100));
  reloaded_receiver.Load();

  CHECK(reloaded_sender->journal.size() == 1);
  CHECK(reloaded_sender->journal[0].event->sequence == 1);
  CHECK(reloaded_sender->journal[0].event->sender.id().id() == 5001);
  CHECK(reloaded_sender->next_local_sequence == 2);
  CHECK(reloaded_sender->journal[0].FindRecipient(recipient_b) != nullptr);
  CHECK(reloaded_sender->journal[0].FindRecipient(recipient_b)->delivery_status ==
        DeliveryStatus::kDelivered);

  CHECK(reloaded_receiver->journal.size() == 2);
  CHECK(reloaded_receiver->journal[0].event->sender.id().id() == 5001);
  CHECK(reloaded_receiver->journal[0].event->sequence == 1);
  CHECK(reloaded_receiver->journal[1].event->sender.id().id() == 5002);
  CHECK(reloaded_receiver->journal[1].event->sequence == 1);
  CHECK(reloaded_receiver->journal[0].recipients.empty());
  CHECK(reloaded_receiver->journal[1].recipients.empty());
  CHECK(reloaded_receiver->value == "AXY");
  CHECK(reloaded_receiver->apply_count == 2);

  SenderObject::ptr reloaded_sender_a = SenderObject::ptr::Declare(
      ae::CreateWith{sender_reload_domain}.with_id(5001));
  reloaded_sender_a.Load();

  AppendValueEvent::ptr third_event = AppendValueEvent::ptr::Create(
      ae::CreateWith{sender_reload_domain}.with_id(202));
  third_event->suffix = "Z";
  third_event->sender = reloaded_sender_a;

  reloaded_sender->CommitEventForTest(
      third_event, ae::TimePoint{std::chrono::microseconds{300}}, {});

  CHECK(reloaded_sender->journal.size() == 2);
  CHECK(third_event->sender.id() == reloaded_sender_a.id());
  CHECK(!third_event->sender.is_loaded());
  CHECK(third_event->sequence == 2);
  CHECK(reloaded_sender->next_local_sequence == 3);
  CHECK(reloaded_sender->journal[0].event->sequence == 1);
  CHECK(reloaded_sender->value == "AXZ");
  CHECK(reloaded_sender->apply_count == 2);

  return EXIT_SUCCESS;
}
