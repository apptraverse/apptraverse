#ifndef APPTRAVERSE_JOURNAL_TRANSPORT_MESSAGE_H_
#define APPTRAVERSE_JOURNAL_TRANSPORT_MESSAGE_H_

#include <cassert>

#include "aether/obj/obj.h"

namespace apptraverse {

class JournalMessageReceiver;

class JournalTransportMessage : public ae::Obj {
  AE_OBJECT(JournalTransportMessage, Obj, 0)

 protected:
  JournalTransportMessage() = default;

 public:
  explicit JournalTransportMessage(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  void Dispatch(JournalMessageReceiver& receiver) { DispatchImpl(receiver); }

 private:
  virtual void DispatchImpl(JournalMessageReceiver& receiver) {
    (void)receiver;
    assert(false &&
           "Concrete journal transport message must implement dispatch");
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_JOURNAL_TRANSPORT_MESSAGE_H_
