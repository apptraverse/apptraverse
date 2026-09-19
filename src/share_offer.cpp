#include "apptraverse/share_offer.h"

#include <cassert>

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(ShareOffer);
APPTRAVERSE_REGISTER(OpenShareOfferEvent);
APPTRAVERSE_REGISTER(SetShareOfferPhaseEvent);

bool LegalPhaseChange(ShareOfferPhase from, ShareOfferPhase to) {
  if (from == ShareOfferPhase::Pending &&
      (to == ShareOfferPhase::Accepted || to == ShareOfferPhase::Rejected ||
       to == ShareOfferPhase::Bound)) {
    return true;
  }
  return from == ShareOfferPhase::Accepted && to == ShareOfferPhase::Complete;
}

}  // namespace

void ForceShareOfferRegistration() {}

void ShareOffer::Apply(OpenShareOfferEvent const& event) {
  assert(GetPhase() == ShareOfferPhase::Unset);
  assert(event.operation_id.is_valid());
  assert(event.node_id.is_valid());
  assert(event.root_class_id != 0);
  assert(!event.remote_endpoint.empty());
  assert(!event.packet.empty());
  assert(event.phase == static_cast<std::uint8_t>(ShareOfferPhase::Pending) ||
         event.phase == static_cast<std::uint8_t>(ShareOfferPhase::Rejected));
  role = event.role;
  phase = event.phase;
  node_id = event.node_id;
  root_class_id = event.root_class_id;
  remote_endpoint = event.remote_endpoint;
  access = event.access;
  remote_link = event.remote_link;
  operation_id = event.operation_id;
  // Packet identity is the opening Event, stable across retry and replay.
  pending_packet_id = event.obj_id;
  pending_packet = event.packet;
  NoteMaterializedChange();
}

bool ShareOffer::CanApply(SetShareOfferPhaseEvent const& event) const {
  return LegalPhaseChange(GetPhase(),
                          static_cast<ShareOfferPhase>(event.phase));
}

void ShareOffer::Apply(SetShareOfferPhaseEvent const& event) {
  assert(CanApply(event));
  phase = event.phase;
  if (event.share_id.is_valid()) {
    share_id = event.share_id;
  }
  NoteMaterializedChange();
}

}  // namespace apptraverse
