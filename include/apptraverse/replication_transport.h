#ifndef APPTRAVERSE_REPLICATION_TRANSPORT_H_
#define APPTRAVERSE_REPLICATION_TRANSPORT_H_

#include "apptraverse/replica_id.h"
#include "apptraverse/replication_message.h"

namespace apptraverse {

class IReplicationTransport {
 public:
  virtual ~IReplicationTransport() = default;

  virtual void Send(ReplicaId recipient, ReplicationMessage::ptr message) = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICATION_TRANSPORT_H_
