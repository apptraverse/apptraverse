#ifndef APPTRAVERSE_GRAPH_SYNCHRONIZER_H_
#define APPTRAVERSE_GRAPH_SYNCHRONIZER_H_

#include <cassert>
#include <utility>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event_record.h"
#include "apptraverse/graph_journal_scanner.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_transport.h"
#include "apptraverse/node.h"

namespace apptraverse {

class GraphSynchronizer {
 public:
  GraphSynchronizer(ae::ObjId recipient, ae::Domain& message_domain,
                    IJournalMessageTransport& transport)
      : recipient_{recipient},
        message_domain_{&message_domain},
        transport_{&transport} {
    assert(recipient_.IsValid());
    assert(message_domain_ != nullptr);
    assert(transport_ != nullptr);
  }

  template <typename RootPtr>
  void Synchronize(RootPtr& root) {
    assert(message_domain_ != nullptr);
    assert(transport_ != nullptr);
    assert(root.is_valid());
    assert(root.is_loaded());

    GraphJournalScanner scanner;
    scanner.VisitPending(
        root, recipient_,
        [&](Node& node, EventRecord& record,
            EventRecipientState& recipient_state) {
          assert(record.origin == EventRecordOrigin::kLocal);
          assert(recipient_state.recipient == recipient_);
          assert(recipient_state.delivery_status == DeliveryStatus::kPending);
          assert(record.event.is_valid());
          assert(record.event.is_loaded());
          assert(record.identity.IsValid());
          assert(node.domain != nullptr);
          assert(record.event.domain() == node.domain);

          auto message = JournalEventMessage::ptr::Create(
              ae::CreateWith{*message_domain_});

          message->target = Node::ptr::MakeFromThis(&node);
          message->target.Reset();
          message->target.SetFlags(ae::ObjFlags::kUnloadedByDefault);

          assert(message->target.is_valid());
          assert(!message->target.is_loaded());

          message->event = record.event;
          message->event.SetFlags(ae::ObjFlags::kUnloadedByDefault);
          message->identity = record.identity;
          message->time = record.time;

          assert(message->identity.IsValid());
          assert(message->event.is_valid());
          assert(message->event.is_loaded());

          JournalTransportMessage::ptr transport_message = message;
          transport_->Send(std::move(transport_message));

          recipient_state.delivery_status = DeliveryStatus::kDelivered;
        });
  }

 private:
  ae::ObjId recipient_;
  ae::Domain* message_domain_;
  IJournalMessageTransport* transport_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_GRAPH_SYNCHRONIZER_H_
