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
#include "apptraverse/event_identity.h"
#include "apptraverse/graph_synchronizer.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/journal_message_transport.h"
#include "apptraverse/journal_transport_message.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class IdentityNode;

class AppendIdentityValueEvent
    : public apptraverse::EventFor<IdentityNode, AppendIdentityValueEvent> {
  AE_OBJECT(AppendIdentityValueEvent, Event, 0)

 protected:
  AppendIdentityValueEvent() = default;

 public:
  explicit AppendIdentityValueEvent(ae::ObjProp prop) : EventFor{prop} {}

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

  void Apply(AppendIdentityValueEvent const& event) {
    value += event.suffix;
    ++apply_count;
  }

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          ae::ObjId origin,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, origin, std::move(recipients));
  }
};

class DuplicatingJournalMessageTransport final
    : public apptraverse::IJournalMessageTransport {
 public:
  DuplicatingJournalMessageTransport(
      ae::Domain& message_domain, ae::RamDomainStorage& transfer_storage,
      ae::Domain& receiver_domain, ae::RamDomainStorage& receiver_storage,
      apptraverse::JournalMessageReceiver& receiver,
      apptraverse::EventIdentity expected_identity)
      : message_domain_{&message_domain},
        transfer_storage_{&transfer_storage},
        receiver_domain_{&receiver_domain},
        receiver_storage_{&receiver_storage},
        receiver_{&receiver},
        expected_identity_{expected_identity} {}

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
    assert(event_message->identity.IsValid());
    assert(event_message->identity == expected_identity_);
    assert(event_message->event.is_valid());
    assert(event_message->event.is_loaded());
    assert((event_message->event.flags() &
            ae::ObjFlags::kUnloadedByDefault) ==
           ae::ObjFlags::kUnloadedByDefault);

    message.Save();
    message_ids.push_back(message.id());

    auto duplicate = apptraverse::JournalEventMessage::ptr::Create(
        ae::CreateWith{*message_domain_});
    duplicate->target = event_message->target;
    duplicate->event = event_message->event;
    duplicate->identity = event_message->identity;
    duplicate->time = event_message->time;

    assert(duplicate->event.is_valid());
    assert(duplicate->event.is_loaded());
    assert(duplicate->event.id() == event_message->event.id());
    assert(duplicate->identity == event_message->identity);
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
  apptraverse::EventIdentity expected_identity_;
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
  using apptraverse::EventIdentity;
  using apptraverse::EventRecordOrigin;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::AppendIdentityValueEvent;
  using apptraverse::test::DuplicatingJournalMessageTransport;
  using apptraverse::test::IdentityNode;

  static_assert(std::is_same_v<decltype(EventIdentity{}.sequence),
                               std::uint32_t>);

  {
    EventIdentity default_identity{};
    CHECK(!default_identity.IsValid());

    EventIdentity zero_sequence{ae::ObjId{5001}, 0};
    CHECK(!zero_sequence.IsValid());

    EventIdentity invalid_origin{ae::ObjId{}, 1};
    CHECK(!invalid_origin.IsValid());

    EventIdentity valid{ae::ObjId{5001}, 1};
    CHECK(valid.IsValid());
  }

  ae::ObjId const origin_a{5001};
  ae::ObjId const origin_b{5002};
  ae::ObjId const recipient_b{6001};

  ae::RamDomainStorage sender_storage;
  ae::RamDomainStorage receiver_storage;
  ae::RamDomainStorage transfer_storage;

  ae::Domain sender_domain{ae::Now(), sender_storage};
  ae::Domain receiver_domain{ae::Now(), receiver_storage};
  ae::Domain message_domain{ae::Now(), transfer_storage};

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

  AppendIdentityValueEvent::ptr first_event =
      AppendIdentityValueEvent::ptr::Create(
          ae::CreateWith{sender_domain}.with_id(200));
  first_event->suffix = "X";

  ae::TimePoint const first_time{std::chrono::microseconds{100}};
  sender->CommitEventForTest(first_event, first_time, origin_a, {recipient_b});

  CHECK(sender->value == "AX");
  CHECK(sender->apply_count == 1);
  CHECK(sender->journal.size() == 1);
  CHECK(sender->journal[0].identity.origin == origin_a);
  CHECK(sender->journal[0].identity.sequence == 1);
  CHECK(sender->journal[0].identity.IsValid());
  CHECK(sender->next_local_sequence == 2);
  CHECK(sender->journal[0].FindRecipient(recipient_b) != nullptr);
  CHECK(sender->journal[0].FindRecipient(recipient_b)->delivery_status ==
        DeliveryStatus::kPending);

  auto const sender_identity = sender->journal[0].identity;

  JournalMessageReceiver message_receiver;
  DuplicatingJournalMessageTransport transport{
      message_domain, transfer_storage, receiver_domain, receiver_storage,
      message_receiver, sender_identity};

  GraphSynchronizer synchronizer{recipient_b, message_domain, transport};
  synchronizer.Synchronize(sender);

  CHECK(transport.send_count == 1);
  CHECK(transport.message_ids.size() == 2);
  CHECK(transport.message_ids[0] != transport.message_ids[1]);
  CHECK(!transport.first_event_loaded_before_receive);
  CHECK(transport.first_event_loaded_after_receive);
  CHECK(!transport.duplicate_event_loaded_before_receive);
  CHECK(!transport.duplicate_event_loaded_after_receive);

  CHECK(sender->journal[0].FindRecipient(recipient_b)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(sender->journal.size() == 1);
  CHECK(sender->value == "AX");
  CHECK(sender->apply_count == 1);

  CHECK(receiver->value == "AX");
  CHECK(receiver->apply_count == 1);
  CHECK(receiver->journal.size() == 1);
  CHECK(receiver->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(receiver->journal[0].recipients.empty());
  CHECK(receiver->journal[0].identity == sender_identity);
  CHECK(receiver->journal[0].event.id().id() == 200);

  CHECK(transfer_storage.state.size() == 3);
  CHECK(ContainsObj(transfer_storage, transport.message_ids[0]));
  CHECK(ContainsObj(transfer_storage, transport.message_ids[1]));
  CHECK(ContainsObj(transfer_storage, 200));
  CHECK(!ContainsObj(transfer_storage, 100));
  CHECK(!ContainsObj(transfer_storage, 1000));

  AppendIdentityValueEvent::ptr second_event =
      AppendIdentityValueEvent::ptr::Create(
          ae::CreateWith{receiver_domain}.with_id(201));
  second_event->suffix = "Y";

  apptraverse::Node::ptr generic_receiver = receiver;
  bool const accepted_duplicate = generic_receiver->AcceptRemoteEvent(
      second_event, ae::TimePoint{std::chrono::microseconds{200}},
      sender_identity);
  CHECK(accepted_duplicate == false);
  CHECK(receiver->value == "AX");
  CHECK(receiver->apply_count == 1);
  CHECK(receiver->journal.size() == 1);

  EventIdentity const other_identity{origin_b, 1};
  bool const accepted_other_origin = generic_receiver->AcceptRemoteEvent(
      second_event, ae::TimePoint{std::chrono::microseconds{200}},
      other_identity);
  CHECK(accepted_other_origin == true);
  CHECK(receiver->value == "AXY");
  CHECK(receiver->apply_count == 2);
  CHECK(receiver->journal.size() == 2);
  CHECK(receiver->journal[1].identity == other_identity);
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
  CHECK(reloaded_sender->journal[0].identity == sender_identity);
  CHECK(reloaded_sender->next_local_sequence == 2);
  CHECK(reloaded_sender->journal[0].FindRecipient(recipient_b) != nullptr);
  CHECK(reloaded_sender->journal[0].FindRecipient(recipient_b)->delivery_status ==
        DeliveryStatus::kDelivered);

  CHECK(reloaded_receiver->journal.size() == 2);
  CHECK(reloaded_receiver->journal[0].identity == sender_identity);
  CHECK(reloaded_receiver->journal[1].identity == other_identity);
  CHECK(reloaded_receiver->journal[0].recipients.empty());
  CHECK(reloaded_receiver->journal[1].recipients.empty());
  CHECK(reloaded_receiver->value == "AXY");
  CHECK(reloaded_receiver->apply_count == 2);

  AppendIdentityValueEvent::ptr third_event =
      AppendIdentityValueEvent::ptr::Create(
          ae::CreateWith{sender_reload_domain}.with_id(202));
  third_event->suffix = "Z";

  reloaded_sender->CommitEventForTest(
      third_event, ae::TimePoint{std::chrono::microseconds{300}}, origin_a,
      {});

  CHECK(reloaded_sender->journal.size() == 2);
  CHECK(reloaded_sender->journal[1].identity.origin == origin_a);
  CHECK(reloaded_sender->journal[1].identity.sequence == 2);
  CHECK(reloaded_sender->next_local_sequence == 3);
  CHECK(reloaded_sender->journal[0].identity.sequence == 1);
  CHECK(reloaded_sender->value == "AXZ");
  CHECK(reloaded_sender->apply_count == 2);

  return EXIT_SUCCESS;
}
