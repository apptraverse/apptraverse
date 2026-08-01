#ifndef APPTRAVERSE_REPLICATION_MESSAGE_H_
#define APPTRAVERSE_REPLICATION_MESSAGE_H_

#include <cassert>
#include <vector>

#include "aether/obj/obj.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/event.h"
#include "apptraverse/event_identity.h"
#include "apptraverse/event_order.h"
#include "apptraverse/node.h"
#include "apptraverse/replica_id.h"

namespace apptraverse {

class ReplicationMessageReceiver;

class ReplicationMessage : public ae::Obj {
  AE_OBJECT(ReplicationMessage, Obj, 0)

 protected:
  ReplicationMessage() = default;

 public:
  explicit ReplicationMessage(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  void Dispatch(ReplicationMessageReceiver& receiver) {
    DispatchImpl(receiver);
  }

 private:
  virtual void DispatchImpl(ReplicationMessageReceiver& receiver) {
    (void)receiver;
    assert(false && "Concrete replication message must implement dispatch");
  }
};

class EventReplicationMessage : public ReplicationMessage {
  AE_OBJECT(EventReplicationMessage, ReplicationMessage, 0)

 protected:
  EventReplicationMessage() = default;

 public:
  explicit EventReplicationMessage(ae::ObjProp prop)
      : ReplicationMessage{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(identity), AE_MMBR(order), AE_MMBR(target),
                    AE_MMBR(event))

  EventIdentity identity;
  EventOrder order;
  Node::ptr target;
  Event::ptr event;

 private:
  void DispatchImpl(ReplicationMessageReceiver& receiver) override;
};

class AckReplicationMessage : public ReplicationMessage {
  AE_OBJECT(AckReplicationMessage, ReplicationMessage, 0)

 protected:
  AckReplicationMessage() = default;

 public:
  explicit AckReplicationMessage(ae::ObjProp prop) : ReplicationMessage{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(identity), AE_MMBR(from_replica))

  EventIdentity identity;
  ReplicaId from_replica;

 private:
  void DispatchImpl(ReplicationMessageReceiver& receiver) override;
};

class BootstrapReplicationMessage : public ReplicationMessage {
  AE_OBJECT(BootstrapReplicationMessage, ReplicationMessage, 0)

 protected:
  BootstrapReplicationMessage() = default;

 public:
  explicit BootstrapReplicationMessage(ae::ObjProp prop)
      : ReplicationMessage{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(root), AE_MMBR(known_shared_ids),
                    AE_MMBR(known_shared_node_ids))

  Node::ptr root;
  std::vector<ae::ObjId> known_shared_ids;
  std::vector<ae::ObjId> known_shared_node_ids;

 private:
  void DispatchImpl(ReplicationMessageReceiver& receiver) override;
};

class ReplicationMessageReceiver {
 public:
  virtual ~ReplicationMessageReceiver() = default;

  void Receive(ReplicationMessage::ptr message) {
    assert(message.is_valid());
    assert(message.is_loaded());
    message->Dispatch(*this);
  }

  virtual void ReceiveEvent(EventReplicationMessage& message) = 0;
  virtual void ReceiveAck(AckReplicationMessage& message) = 0;
  virtual void ReceiveBootstrap(BootstrapReplicationMessage& message) = 0;
};

inline void EventReplicationMessage::DispatchImpl(
    ReplicationMessageReceiver& receiver) {
  receiver.ReceiveEvent(*this);
}

inline void AckReplicationMessage::DispatchImpl(
    ReplicationMessageReceiver& receiver) {
  receiver.ReceiveAck(*this);
}

inline void BootstrapReplicationMessage::DispatchImpl(
    ReplicationMessageReceiver& receiver) {
  receiver.ReceiveBootstrap(*this);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_MESSAGE_H_
