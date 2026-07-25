#ifndef APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_
#define APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_

#include "aether/clock.h"

#include "apptraverse/event.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/node.h"

namespace apptraverse {

class JournalEventMessage : public JournalTransportMessage {
  AE_OBJECT(JournalEventMessage, JournalTransportMessage, 0)

 protected:
  JournalEventMessage() = default;

 public:
  explicit JournalEventMessage(ae::ObjProp prop) : JournalTransportMessage{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(target), AE_MMBR(event), AE_MMBR(time))

  Node::ptr target;
  Event::ptr event;
  ae::TimePoint time{};

 private:
  void DispatchImpl(JournalMessageReceiver& receiver) override {
    receiver.ReceiveEvent(*this);
  }
};

inline void JournalMessageReceiver::ReceiveEvent(JournalEventMessage& message) {
  assert(message.target.is_valid());
  assert(message.event.is_valid());
  assert(message.target.domain() == message.event.domain());

  message.target.Load();
  assert(message.target.is_loaded());

  message.event.Load();
  assert(message.event.is_loaded());
  assert(message.event->HasValidIdentity());

  message.target->AcceptRemoteEvent(message.event, message.time);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_
