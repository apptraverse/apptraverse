#include "apptraverse/journal_synchronizer.h"

#include <cassert>
#include <utility>

#include "apptraverse/event_object_codec.h"

namespace apptraverse {

JournalSynchronizer::JournalSynchronizer(Node& node, ae::Domain& domain,
                                         ae::IDomainStorage& storage,
                                         IEventTransport& transport)
    : node_{&node},
      domain_{&domain},
      storage_{&storage},
      transport_{&transport} {
  transport_->SetReceiver(this);
}

void JournalSynchronizer::FlushPending() {
  assert(node_ != nullptr);
  assert(transport_ != nullptr);

  for (std::size_t i = 0; i < node_->JournalSize(); ++i) {
    auto const& record = node_->JournalRecordAt(i);
    if (record.delivery_state() != EventDeliveryState::kPending) {
      continue;
    }
    if (record.origin() != node_->replica_id()) {
      continue;
    }

    // Mark sent before SendEvent so a synchronous confirmation cannot be
    // overwritten by a later transition back to kSent.
    node_->MarkSent(record.identity());

    EventTransportMessage message;
    message.target_node_id = node_->obj_id;
    message.identity = record.identity();
    message.logical_time = record.logical_time();
    message.event_object = EncodeEventObject(record.event());
    transport_->SendEvent(std::move(message));
  }
}

void JournalSynchronizer::OnEvent(EventTransportMessage message) {
  assert(node_ != nullptr);
  assert(domain_ != nullptr);
  assert(storage_ != nullptr);
  assert(transport_ != nullptr);
  assert(message.target_node_id == node_->obj_id);
  assert(message.identity.IsValid());

  auto destination_event_id = ae::ObjId::GenerateUnique();
  auto event = DecodeEventObject(*domain_, *storage_, message.event_object,
                                 destination_event_id);

  EventRecord record{std::move(event), message.identity, message.logical_time,
                     EventDeliveryState::kConfirmed};
  node_->AcceptRemoteEvent(std::move(record));

  EventConfirmation confirmation;
  confirmation.target_node_id = message.target_node_id;
  confirmation.identity = message.identity;
  transport_->SendConfirmation(std::move(confirmation));
}

void JournalSynchronizer::OnConfirmation(EventConfirmation confirmation) {
  assert(node_ != nullptr);
  assert(confirmation.target_node_id == node_->obj_id);
  assert(confirmation.identity.IsValid());
  node_->MarkConfirmed(confirmation.identity);
}

}  // namespace apptraverse
