#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/graph_journal_scanner.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/journal_transport_message.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class TransferNode;

class AppendNameEvent
    : public apptraverse::EventFor<TransferNode, AppendNameEvent> {
  AE_OBJECT(AppendNameEvent, Event, 0)

 protected:
  AppendNameEvent() = default;

 public:
  explicit AppendNameEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class TransferNode : public apptraverse::NodeFor<TransferNode> {
  AE_OBJECT(TransferNode, Node, 0)

 protected:
  TransferNode() = default;

 public:
  explicit TransferNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(AppendNameEvent const& event) { name += event.suffix; }

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time) {
    CommitEvent(std::move(event), time);
  }
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

void ImportWholeStorage(ae::RamDomainStorage const& source,
                        ae::RamDomainStorage& destination) {
  destination.state.insert(source.state.begin(), source.state.end());
}

static_assert(std::is_base_of_v<ae::Obj, apptraverse::JournalTransportMessage>);
static_assert(std::is_base_of_v<apptraverse::JournalTransportMessage,
                                apptraverse::JournalEventMessage>);
static_assert(
    !std::is_base_of_v<apptraverse::Event, apptraverse::JournalEventMessage>);
static_assert(
    !std::is_base_of_v<apptraverse::Node, apptraverse::JournalEventMessage>);
static_assert(
    !std::is_base_of_v<ae::Obj, apptraverse::JournalMessageReceiver>);

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphJournalScanner;
  using apptraverse::JournalEventMessage;
  using apptraverse::JournalTransportMessage;
  using apptraverse::test::AppendNameEvent;
  using apptraverse::test::TransferNode;

  ae::RamDomainStorage sender_storage;
  ae::RamDomainStorage receiver_storage;
  ae::RamDomainStorage transfer_storage;

  ae::Domain sender_domain{ae::Now(), sender_storage};
  ae::Domain receiver_domain{ae::Now(), receiver_storage};
  ae::Domain transfer_domain{ae::Now(), transfer_storage};

  TransferNode::ptr sender_base =
      TransferNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(1000));
  CHECK(static_cast<bool>(sender_base));
  sender_base->name = "Uninitialized sender base";

  TransferNode::ptr sender_node =
      TransferNode::ptr::Create(ae::CreateWith{sender_domain}.with_id(100));
  CHECK(static_cast<bool>(sender_node));
  sender_node->name = "Alice";
  sender_node->base = sender_base;
  sender_node->CaptureBaseStateForTest();

  CHECK(sender_node->name == "Alice");
  CHECK(sender_base->name == "Alice");
  CHECK(sender_node->journal.empty());

  TransferNode::ptr receiver_base =
      TransferNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(1000));
  CHECK(static_cast<bool>(receiver_base));
  receiver_base->name = "Uninitialized receiver base";

  TransferNode::ptr receiver_node =
      TransferNode::ptr::Create(ae::CreateWith{receiver_domain}.with_id(100));
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

  AppendNameEvent::ptr sender_event =
      AppendNameEvent::ptr::Create(ae::CreateWith{sender_domain}.with_id(200));
  CHECK(static_cast<bool>(sender_event));
  sender_event->suffix = " Cooper";

  ae::TimePoint const event_time{std::chrono::microseconds{100}};
  sender_node->CommitEventForTest(sender_event, event_time);

  CHECK(sender_node->name == "Alice Cooper");
  CHECK(sender_node->journal.size() == 1);
  CHECK(sender_node->journal[0].delivery_status == DeliveryStatus::kPending);
  CHECK(receiver_node->name == "Alice");
  CHECK(receiver_node->journal.empty());

  GraphJournalScanner scanner;
  int callback_count = 0;
  JournalEventMessage::ptr message;

  scanner.VisitPending(sender_node, [&](apptraverse::Node& node,
                                        apptraverse::EventRecord& record) {
    ++callback_count;
    message = JournalEventMessage::ptr::Create(
        ae::CreateWith{transfer_domain}.with_id(900));

    message->target = apptraverse::Node::ptr::MakeFromThis(&node);
    message->target.Reset();
    message->target.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    message->event = record.event;
    message->time = record.time;
  });

  CHECK(callback_count == 1);
  CHECK(static_cast<bool>(message));

  CHECK(message->target.is_valid());
  CHECK(message->target.id().id() == 100);
  CHECK(!message->target.is_loaded());
  CHECK(message->event.is_valid());
  CHECK(message->event.is_loaded());
  CHECK(message->event.id().id() == 200);
  CHECK(message->time == event_time);
  CHECK(sender_node.is_loaded());
  CHECK(sender_event.is_loaded());

  message.Save();

  CHECK(ContainsObj(transfer_storage, 900));
  CHECK(ContainsObj(transfer_storage, 200));
  CHECK(!ContainsObj(transfer_storage, 100));
  CHECK(!ContainsObj(transfer_storage, 1000));
  CHECK(transfer_storage.state.size() == 2);

  ImportWholeStorage(transfer_storage, receiver_storage);

  JournalTransportMessage::ptr incoming_message =
      JournalTransportMessage::ptr::Declare(
          ae::CreateWith{receiver_domain}.with_id(900));
  incoming_message.Load();

  CHECK(incoming_message.is_valid());
  CHECK(incoming_message.is_loaded());

  auto* incoming_event_message =
      incoming_message.Load().as<JournalEventMessage>();
  CHECK(incoming_event_message != nullptr);
  CHECK(incoming_message->GetClassId() == JournalEventMessage::kClassId);

  CHECK(incoming_event_message->target.is_valid());
  CHECK(incoming_event_message->target.id().id() == 100);
  CHECK(!incoming_event_message->target.is_loaded());
  CHECK(incoming_event_message->target.domain() == &receiver_domain);
  CHECK(incoming_event_message->event.is_valid());
  CHECK(incoming_event_message->event.is_loaded());
  CHECK(incoming_event_message->event.id().id() == 200);
  CHECK(incoming_event_message->event.domain() == &receiver_domain);
  CHECK(incoming_event_message->time == event_time);

  auto* receiver_append =
      incoming_event_message->event.Load().as<AppendNameEvent>();
  CHECK(receiver_append != nullptr);
  CHECK(receiver_append->suffix == " Cooper");
  CHECK(incoming_event_message->event.Load().get() !=
        sender_event.Load().get());

  auto* receiver_node_address = receiver_node.Load().get();
  CHECK(receiver_node_address != nullptr);
  CHECK(receiver_node->name == "Alice");
  CHECK(!incoming_event_message->target.is_loaded());

  apptraverse::JournalMessageReceiver receiver;
  receiver.Receive(incoming_message);

  CHECK(incoming_event_message->target.is_loaded());
  CHECK(incoming_event_message->target.Load().get() == receiver_node_address);
  CHECK(incoming_event_message->target.Load().get() !=
        sender_node.Load().get());
  CHECK(incoming_event_message->target.Load().as<TransferNode>() != nullptr);

  CHECK(receiver_node->name == "Alice Cooper");
  CHECK(receiver_node->journal.size() == 1);
  CHECK(receiver_node->journal[0].event.id().id() == 200);
  CHECK(receiver_node->journal[0].time == event_time);
  CHECK(receiver_node->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(receiver_node->journal[0].event.Load().get() ==
        incoming_event_message->event.Load().get());
  CHECK(incoming_event_message->event.is_valid());
  CHECK(incoming_event_message->event.is_loaded());

  CHECK(sender_node->name == "Alice Cooper");
  CHECK(sender_node->journal.size() == 1);
  CHECK(sender_node->journal[0].delivery_status == DeliveryStatus::kPending);

  int sender_pending = 0;
  scanner.VisitPending(sender_node, [&](apptraverse::Node&,
                                        apptraverse::EventRecord&) {
    ++sender_pending;
  });
  CHECK(sender_pending == 1);

  int receiver_pending = 0;
  scanner.VisitPending(receiver_node, [&](apptraverse::Node&,
                                          apptraverse::EventRecord&) {
    ++receiver_pending;
  });
  CHECK(receiver_pending == 0);

  receiver_node.Save();

  ae::Domain reload_domain{ae::Now(), receiver_storage};
  apptraverse::Node::ptr reloaded =
      apptraverse::Node::ptr::Declare(ae::CreateWith{reload_domain}.with_id(100));
  reloaded.Load();

  CHECK(reloaded.is_valid());
  CHECK(reloaded.is_loaded());
  CHECK(reloaded->GetClassId() == TransferNode::kClassId);

  auto* reloaded_node = reloaded.Load().as<TransferNode>();
  CHECK(reloaded_node != nullptr);
  CHECK(reloaded_node->name == "Alice Cooper");
  CHECK(reloaded_node->base.id().id() == 1000);
  CHECK(reloaded_node->journal.size() == 1);
  CHECK(reloaded_node->journal[0].event.id().id() == 200);
  CHECK(reloaded_node->journal[0].delivery_status == DeliveryStatus::kDelivered);

  auto* reloaded_event =
      reloaded_node->journal[0].event.Load().as<AppendNameEvent>();
  CHECK(reloaded_event != nullptr);
  CHECK(reloaded_event->suffix == " Cooper");

  auto* reloaded_base = reloaded_node->base.Load().as<TransferNode>();
  CHECK(reloaded_base != nullptr);
  CHECK(reloaded_base->name == "Alice");
  CHECK(!reloaded_base->base.is_valid());
  CHECK(reloaded_base->journal.empty());

  CHECK(sender_event.domain() == &sender_domain);
  CHECK(incoming_event_message->event.domain() == &receiver_domain);
  CHECK(sender_event.Load().get() !=
        incoming_event_message->event.Load().get());
  CHECK(sender_node->journal[0].delivery_status == DeliveryStatus::kPending);
  CHECK(incoming_event_message->target.Load().get() == receiver_node_address);
  CHECK(incoming_event_message->target.Load().get() !=
        sender_node.Load().get());

  return EXIT_SUCCESS;
}
