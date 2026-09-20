#ifndef APPTRAVERSE_SYNC_FRAME_H_
#define APPTRAVERSE_SYNC_FRAME_H_

#include <cstdint>
#include <string>
#include <vector>

#include "aether-objects/obj/obj_id.h"

#include "apptraverse/shared_event_id.h"

namespace apptraverse {

// Shared synchronization protocol v1: admission, initial state, catch-up,
// standalone Event, and topology Events. Catch-up agrees a relationship
// between replicas that already hold the node. It is not a second snapshot.
// Dynamic object graphs beyond that topology and presence are later.
inline constexpr std::uint8_t kSyncProtocolVersion = 1;

enum class SyncFrameType : std::uint8_t {
  kNodeState = 1,
  kAck = 2,
  kEvent = 3,
  kShareOffer = 4,
  kShareDecision = 5,
  kShareRequest = 6,
  // Events this sender already has, for a relationship that is not driven by
  // a NodeState snapshot. Both sides already hold the node.
  kShareCatchUp = 7,
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

// Incremental standalone Event for one already-synchronized SharedNode.
// Logical identity is SharedEventId. The sender Event ObjId is not on the
// wire as a receiver storage key.
struct EventFrame {
  ae::ObjId packet_id;
  ae::ObjId target_node_id;
  ae::ObjId destination_share_id;
  SharedEventId identity;
  std::uint64_t timestamp_us{0};
  std::uint32_t event_class_id{0};
  std::vector<std::uint8_t> payload;
};

// Decoders validate untrusted bytes and return false instead of asserting.
bool PeekSyncFrameType(std::vector<std::uint8_t> const& bytes,
                       SyncFrameType& out);

std::vector<std::uint8_t> EncodeNodeStateFrame(NodeStateFrame const& frame);
bool DecodeNodeStateFrame(std::vector<std::uint8_t> const& bytes,
                          NodeStateFrame& out);

std::vector<std::uint8_t> EncodeAckFrame(AckFrame const& frame);
bool DecodeAckFrame(std::vector<std::uint8_t> const& bytes, AckFrame& out);

std::vector<std::uint8_t> EncodeEventFrame(EventFrame const& frame);
bool DecodeEventFrame(std::vector<std::uint8_t> const& bytes, EventFrame& out);

// Offer one existing SharedNode to the transport peer. Access is the right
// the responder will hold. No source field: the sender cannot name itself.
struct ShareOfferFrame {
  ae::ObjId packet_id;
  ae::ObjId operation_id;
  ae::ObjId target_node_id;
  std::uint32_t root_class_id{0};
  std::uint8_t access{0};
};

// Accept or reject one operation. Identity fields must echo the offer.
struct ShareDecisionFrame {
  ae::ObjId packet_id;
  ae::ObjId operation_id;
  ae::ObjId target_node_id;
  std::uint32_t root_class_id{0};
  std::uint8_t access{0};
  bool accepted{false};
};

std::vector<std::uint8_t> EncodeShareOfferFrame(ShareOfferFrame const& frame);
bool DecodeShareOfferFrame(std::vector<std::uint8_t> const& bytes,
                           ShareOfferFrame& out);

// Same fields as ShareOffer. Class may be 0: the holder names it in the
// decision. Still no source field.
std::vector<std::uint8_t> EncodeShareRequestFrame(ShareOfferFrame const& frame);
bool DecodeShareRequestFrame(std::vector<std::uint8_t> const& bytes,
                             ShareOfferFrame& out);

std::vector<std::uint8_t> EncodeShareDecisionFrame(
    ShareDecisionFrame const& frame);
bool DecodeShareDecisionFrame(std::vector<std::uint8_t> const& bytes,
                              ShareDecisionFrame& out);

// Lists SharedEventIds the sender already applied. The receiver marks them
// delivered on its outgoing sync toward the sender and acknowledges. The
// sender's own covered set stays empty, so that acknowledgement does not
// suppress events the peer still needs.
struct ShareCatchUpFrame {
  ae::ObjId packet_id;
  ae::ObjId target_node_id;
  ae::ObjId destination_share_id;
  std::vector<SharedEventId> have;
};

std::vector<std::uint8_t> EncodeShareCatchUpFrame(
    ShareCatchUpFrame const& frame);
bool DecodeShareCatchUpFrame(std::vector<std::uint8_t> const& bytes,
                             ShareCatchUpFrame& out);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SYNC_FRAME_H_
