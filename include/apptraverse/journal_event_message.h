#ifndef APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_
#define APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_

#include "aether/clock.h"

#include "apptraverse/event.h"
#include "apptraverse/journal_transport_message.h"
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
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_EVENT_MESSAGE_H_
