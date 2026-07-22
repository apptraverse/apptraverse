#ifndef APPTRAVERSE_EVENT_OBJECT_CODEC_H_
#define APPTRAVERSE_EVENT_OBJECT_CODEC_H_

#include "aether/obj/domain.h"
#include "aether/obj/idomain_storage.h"

#include "apptraverse/event.h"
#include "apptraverse/event_transport.h"

namespace apptraverse {

/**
 * \brief Encode/decode a concrete Event via existing Æther object layers.
 *
 * Encode captures factory save output into an in-memory blob storage.
 * Decode writes those layers into the destination IDomainStorage and loads an
 * independent Event::ptr bound to the destination Domain.
 */
EventObjectPayload EncodeEventObject(Event::ptr const& event);

Event::ptr DecodeEventObject(ae::Domain& destination_domain,
                             ae::IDomainStorage& destination_storage,
                             EventObjectPayload const& payload,
                             ae::ObjId destination_event_id);

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_OBJECT_CODEC_H_
