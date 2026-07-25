#ifndef APPTRAVERSE_JOURNAL_MESSAGE_RECEIVER_H_
#define APPTRAVERSE_JOURNAL_MESSAGE_RECEIVER_H_

#include <cassert>

#include "apptraverse/journal_transport_message.h"

namespace apptraverse {

class JournalEventMessage;

class JournalMessageReceiver {
 public:
  void Receive(JournalTransportMessage::ptr message) {
    assert(message.is_valid());
    assert(message.is_loaded());

    message->Dispatch(*this);
  }

 private:
  friend class JournalEventMessage;

  void ReceiveEvent(JournalEventMessage& message);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_MESSAGE_RECEIVER_H_
