#ifndef APPTRAVERSE_UI_MATERIALIZED_H_
#define APPTRAVERSE_UI_MATERIALIZED_H_

#include <cstdint>
#include <vector>

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/publication_channel.h"

namespace apptraverse {

void SerializeMaterializedObject(ae::Obj const& object, ByteSink& out);

void DeserializeMaterializedObject(ae::Obj& object, ByteSource& in,
                                   ae::Domain& ui_domain);

ae::Ptr<ae::Obj> EnsureUiObject(ae::Domain& ui_domain, ae::ObjId id,
                                std::uint32_t class_id);

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_MATERIALIZED_H_
