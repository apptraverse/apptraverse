#ifndef APPTRAVERSE_SYNC_FRAME_H_
#define APPTRAVERSE_SYNC_FRAME_H_

#include <cstdint>
#include <vector>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse {

// Shared synchronization protocol v1: initial state only. Incremental Event
// replication, presence, and access negotiation are not part of it yet.
inline constexpr std::uint8_t kSyncProtocolVersion = 1;

enum class SyncFrameType : std::uint8_t {
  kNodeState = 1,
  kAck = 2,
};

// Initial state of one SharedNode for one Share relationship.
//
// target_node_id says which SharedNode is synchronized: one Link carries
// several SharedNodes, so the source endpoint alone cannot identify it.
// destination_share_id is the Share relationship the state is being sent for.
// Relationship identity is shared, so sender and receiver name it the same.
struct NodeStateFrame {
  ae::ObjId packet_id;
  ae::ObjId target_node_id;
  ae::ObjId destination_share_id;
  std::vector<std::uint8_t> payload;
};

struct AckFrame {
  ae::ObjId packet_id;
  ae::ObjId target_node_id;
  ae::ObjId destination_share_id;
};

// Decoders validate untrusted bytes and return false instead of asserting.
bool PeekSyncFrameType(std::vector<std::uint8_t> const& bytes,
                       SyncFrameType& out);

std::vector<std::uint8_t> EncodeNodeStateFrame(NodeStateFrame const& frame);
bool DecodeNodeStateFrame(std::vector<std::uint8_t> const& bytes,
                          NodeStateFrame& out);

std::vector<std::uint8_t> EncodeAckFrame(AckFrame const& frame);
bool DecodeAckFrame(std::vector<std::uint8_t> const& bytes, AckFrame& out);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SYNC_FRAME_H_
