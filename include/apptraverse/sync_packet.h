#ifndef APPTRAVERSE_SYNC_PACKET_H_
#define APPTRAVERSE_SYNC_PACKET_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_graph_copy_detail.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class NodeStatePacket;
class EventPacket;
class AckPacket;

class SyncPacketHandler {
 public:
  virtual ~SyncPacketHandler() = default;
  virtual void Handle(NodeStatePacket const& packet) = 0;
  virtual void Handle(EventPacket const& packet) = 0;
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

  void PrepareSyncGraph(ae::IDomainStorage* dest_for_refs,
                        SharedCopyMode mode) {
    detail::PrepareSyncGraphContext ctx{dest_for_refs, mode, {}};
    PrepareSyncGraph(ctx);
  }

  void PrepareSyncGraph(detail::PrepareSyncGraphContext& ctx) {
    PrepareSyncGraphImpl(ctx);
  }

 private:
  virtual void PrepareSyncGraphImpl(detail::PrepareSyncGraphContext& ctx) = 0;
};

template <typename ConcretePacket>
class SyncPacketFor : public SyncPacket {
 protected:
  SyncPacketFor() = default;
  explicit SyncPacketFor(ae::ObjProp prop) : SyncPacket{prop} {}

 public:
  void Dispatch(SyncPacketHandler& handler) const override {
    handler.Handle(static_cast<ConcretePacket const&>(*this));
  }

 private:
  void PrepareSyncGraphImpl(detail::PrepareSyncGraphContext& ctx) override {
    detail::PrepareSyncGraphObject(static_cast<ConcretePacket&>(*this), ctx);
  }
};

class NodeStatePacket : public SyncPacketFor<NodeStatePacket> {
  APPTRAVERSE_OBJECT(NodeStatePacket, SyncPacket, 0)

 protected:
  NodeStatePacket() = default;

 public:
  explicit NodeStatePacket(ae::ObjProp prop) : SyncPacketFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(node))

  Node::ptr node;
};

class EventPacket : public SyncPacketFor<EventPacket> {
  APPTRAVERSE_OBJECT(EventPacket, SyncPacket, 0)

 protected:
  EventPacket() = default;

 public:
  explicit EventPacket(ae::ObjProp prop) : SyncPacketFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(target_node_id), AE_MMBR(timestamp_us),
                    AE_MMBR(event))

  ae::ObjId target_node_id;
  std::uint64_t timestamp_us{0};
  Event::ptr event;
};

class AckPacket : public SyncPacketFor<AckPacket> {
  APPTRAVERSE_OBJECT(AckPacket, SyncPacket, 0)

 protected:
  AckPacket() = default;

 public:
  explicit AckPacket(ae::ObjProp prop) : SyncPacketFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(acknowledged_packet_id))

  ae::ObjId acknowledged_packet_id;
};

using SerializedSyncPacket = std::vector<std::uint8_t>;

// Owns the temporary Domain that holds a decoded packet graph.
struct DecodedSyncPacket {
  std::unique_ptr<ae::RamDomainStorage> storage;
  std::unique_ptr<ae::Domain> domain;
  SyncPacket::ptr packet;
};

// Stateless codec: Decode never keeps a long-lived packet Domain.
class SyncPacketCodec {
 public:
  SerializedSyncPacket Encode(SyncPacket::ptr packet);
  DecodedSyncPacket Decode(SerializedSyncPacket const& bytes);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SYNC_PACKET_H_
