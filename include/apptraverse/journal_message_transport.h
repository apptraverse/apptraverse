#ifndef APPTRAVERSE_JOURNAL_MESSAGE_TRANSPORT_H_
#define APPTRAVERSE_JOURNAL_MESSAGE_TRANSPORT_H_

#include "apptraverse/journal_transport_message.h"

namespace apptraverse {

class IJournalMessageTransport {
 public:
  virtual ~IJournalMessageTransport() = default;

  virtual void Send(JournalTransportMessage::ptr message) = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_MESSAGE_TRANSPORT_H_
