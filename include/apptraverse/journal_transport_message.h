#ifndef APPTRAVERSE_JOURNAL_TRANSPORT_MESSAGE_H_
#define APPTRAVERSE_JOURNAL_TRANSPORT_MESSAGE_H_

#include "aether/obj/obj.h"

namespace apptraverse {

class JournalTransportMessage : public ae::Obj {
  AE_OBJECT(JournalTransportMessage, Obj, 0)

 protected:
  JournalTransportMessage() = default;

 public:
  explicit JournalTransportMessage(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_TRANSPORT_MESSAGE_H_
