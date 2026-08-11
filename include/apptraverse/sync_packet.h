#ifndef APPTRAVERSE_SYNC_PACKET_H_
#define APPTRAVERSE_SYNC_PACKET_H_

#include <cstdint>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/object_state.h"

namespace apptraverse {

class NodeStatePacket;
class EventPacket;
class NodeStateRequestPacket;
class AckPacket;

class SyncPacketHandler {
 public:
  virtual ~SyncPacketHandler() = default;
  virtual void Handle(NodeStatePacket const& packet) = 0;
  virtual void Handle(EventPacket const& packet) = 0;
  virtual void Handle(NodeStateRequestPacket const& packet) = 0;
  virtual void Handle(AckPacket const& packet) = 0;
};

// Base synchronization packet. Packet identity is this object's ObjId.
class SyncPacket : public ae::Obj {
  APPTRAVERSE_OBJECT(SyncPacket, ae::Obj, 0)

 protected:
  SyncPacket() = default;

 public:
  explicit SyncPacket(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  virtual void Dispatch(SyncPacketHandler& handler) const = 0;
};

class NodeStatePacket : public SyncPacket {
  APPTRAVERSE_OBJECT(NodeStatePacket, SyncPacket, 0)

 protected:
  NodeStatePacket() = default;

 public:
  explicit NodeStatePacket(ae::ObjProp prop) : SyncPacket{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(state), AE_MMBR(required_nodes))

  ObjectState state;
  std::vector<ae::ObjId> required_nodes;

  void Dispatch(SyncPacketHandler& handler) const override {
    handler.Handle(*this);
  }
};

class EventPacket : public SyncPacket {
  APPTRAVERSE_OBJECT(EventPacket, SyncPacket, 0)

 protected:
  EventPacket() = default;

 public:
  explicit EventPacket(ae::ObjProp prop) : SyncPacket{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(target_node_id), AE_MMBR(timestamp_us),
                    AE_MMBR(state), AE_MMBR(required_nodes))

  ae::ObjId target_node_id;
  std::uint64_t timestamp_us{0};
  EventState state;
  std::vector<ae::ObjId> required_nodes;

  void Dispatch(SyncPacketHandler& handler) const override {
    handler.Handle(*this);
  }
};

class NodeStateRequestPacket : public SyncPacket {
  APPTRAVERSE_OBJECT(NodeStateRequestPacket, SyncPacket, 0)

 protected:
  NodeStateRequestPacket() = default;

 public:
  explicit NodeStateRequestPacket(ae::ObjProp prop) : SyncPacket{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(requested_node_id))

  ae::ObjId requested_node_id;

  void Dispatch(SyncPacketHandler& handler) const override {
    handler.Handle(*this);
  }
};

class AckPacket : public SyncPacket {
  APPTRAVERSE_OBJECT(AckPacket, SyncPacket, 0)

 protected:
  AckPacket() = default;

 public:
  explicit AckPacket(ae::ObjProp prop) : SyncPacket{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(acknowledged_packet_id))

  ae::ObjId acknowledged_packet_id;

  void Dispatch(SyncPacketHandler& handler) const override {
    handler.Handle(*this);
  }
};

using SerializedSyncPacket = std::vector<std::uint8_t>;

// Separate Domain for packet objects; never shared with application Nodes.
class SyncPacketCodec {
 public:
  SyncPacketCodec();

  ae::Domain& domain() { return domain_; }
  ae::RamDomainStorage& storage() { return storage_; }

  SerializedSyncPacket Encode(SyncPacket::ptr packet);
  SyncPacket::ptr Decode(SerializedSyncPacket const& bytes);

 private:
  ae::RamDomainStorage storage_;
  ae::Domain domain_;
};

SerializedSyncPacket EncodeObjectState(ObjectState const& state);
ObjectState DecodeObjectState(SerializedSyncPacket const& bytes);

std::vector<ae::ObjId> DiscoverSharedDependencies(Node::ptr node);
std::vector<ae::ObjId> DiscoverSharedDependencies(Event::ptr event);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SYNC_PACKET_H_
