#ifndef APPTRAVERSE_SHARE_OFFER_H_
#define APPTRAVERSE_SHARE_OFFER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "apptraverse/event_for.h"
#include "apptraverse/link.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_node.h"

namespace apptraverse {

// One offer of one SharedNode to one remote endpoint.
//
// operation, node, and share are different identities. This object's ObjId is
// the operation on the initiator. The responder stores that same operation id
// in `operation_id` and keeps its own object id. `share_id` is filled only
// after AddShare, and is never used as the operation id.
//
// Local to the replica that records it. Not a member of the shared graph.
enum class ShareOfferRole : std::uint8_t {
  Initiator = 1,
  Responder = 2,
};

enum class ShareOfferPhase : std::uint8_t {
  Unset = 0,
  Pending = 1,
  Accepted = 2,
  Rejected = 3,
  Bound = 4,
  Complete = 5,
};

class OpenShareOfferEvent;
class SetShareOfferPhaseEvent;

class ShareOffer : public NodeFor<ShareOffer> {
  APPTRAVERSE_OBJECT(ShareOffer, Node, 0)

 protected:
  ShareOffer() = default;

 public:
  explicit ShareOffer(ae::ObjProp prop) : NodeFor{prop} {}

  // Visitor follows the Link edge. Scalar admission fields travel in Load/Save.
  AE_OBJECT_REFLECT(AE_MMBR(remote_link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, role, phase, node_id, root_class_id, remote_endpoint, access,
        remote_link, operation_id, share_id, pending_packet_id, pending_packet);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, role, phase, node_id, root_class_id, remote_endpoint, access,
        remote_link, operation_id, share_id, pending_packet_id, pending_packet);
  }

  std::uint8_t role{0};
  std::uint8_t phase{static_cast<std::uint8_t>(ShareOfferPhase::Unset)};
  ae::ObjId node_id;
  std::uint32_t root_class_id{0};
  std::string remote_endpoint;
  std::uint8_t access{static_cast<std::uint8_t>(ShareAccess::ReadWrite)};
  Link::ptr remote_link;
  ae::ObjId operation_id;
  ae::ObjId share_id;
  ae::ObjId pending_packet_id;
  std::vector<std::uint8_t> pending_packet;

  ShareOfferRole GetRole() const {
    return static_cast<ShareOfferRole>(role);
  }
  ShareOfferPhase GetPhase() const {
    return static_cast<ShareOfferPhase>(phase);
  }
  ShareAccess GetAccess() const {
    return static_cast<ShareAccess>(access);
  }

  void Apply(OpenShareOfferEvent const& event);
  bool CanApply(SetShareOfferPhaseEvent const& event) const;
  void Apply(SetShareOfferPhaseEvent const& event);
};

// Opens the offer and freezes the exact packet that retries resend.
// packet identity is this Event's ObjId.
class OpenShareOfferEvent : public EventFor<ShareOffer, OpenShareOfferEvent> {
  APPTRAVERSE_OBJECT(OpenShareOfferEvent, Event, 0)

 protected:
  OpenShareOfferEvent() = default;

 public:
  explicit OpenShareOfferEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(remote_link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, role, phase, node_id, root_class_id, remote_endpoint, access,
        remote_link, operation_id, packet);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, role, phase, node_id, root_class_id, remote_endpoint, access,
        remote_link, operation_id, packet);
  }

  std::uint8_t role{0};
  std::uint8_t phase{static_cast<std::uint8_t>(ShareOfferPhase::Pending)};
  ae::ObjId node_id;
  std::uint32_t root_class_id{0};
  std::string remote_endpoint;
  std::uint8_t access{static_cast<std::uint8_t>(ShareAccess::ReadWrite)};
  Link::ptr remote_link;
  ae::ObjId operation_id;
  std::vector<std::uint8_t> packet;
};

class SetShareOfferPhaseEvent
    : public EventFor<ShareOffer, SetShareOfferPhaseEvent> {
  APPTRAVERSE_OBJECT(SetShareOfferPhaseEvent, Event, 0)

 protected:
  SetShareOfferPhaseEvent() = default;

 public:
  explicit SetShareOfferPhaseEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(phase), AE_MMBR(share_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, phase, share_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, phase, share_id);
  }

  std::uint8_t phase{static_cast<std::uint8_t>(ShareOfferPhase::Unset)};
  ae::ObjId share_id;
};

void ForceShareOfferRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARE_OFFER_H_
