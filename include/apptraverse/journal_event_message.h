#ifndef APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_
#define APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_

#include "aether/clock.h"

#include "apptraverse/event.h"
#include "apptraverse/event_identity.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/node.h"

namespace apptraverse {

class JournalEventMessage : public JournalTransportMessage {
  AE_OBJECT(JournalEventMessage, JournalTransportMessage, 0)

 protected:
  JournalEventMessage() = default;

 public:
  explicit JournalEventMessage(ae::ObjProp prop) : JournalTransportMessage{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(target), AE_MMBR(event), AE_MMBR(identity),
                    AE_MMBR(time))

  Node::ptr target;
  Event::ptr event;
  EventIdentity identity;
  ae::TimePoint time{};

 private:
  void DispatchImpl(JournalMessageReceiver& receiver) override {
    receiver.ReceiveEvent(*this);
  }
};

inline void JournalMessageReceiver::ReceiveEvent(JournalEventMessage& message) {
  assert(message.target.is_valid());
  assert(message.event.is_valid());
  assert(message.identity.IsValid());
  assert(message.target.domain() == message.event.domain());

  message.target.Load();

  assert(message.target.is_loaded());

  if (message.target->ContainsEvent(message.identity)) {
    return;
  }

  message.event.Load();

  assert(message.event.is_loaded());

  bool const accepted = message.target->AcceptRemoteEvent(
      message.event, message.time, message.identity);
  assert(accepted);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_
