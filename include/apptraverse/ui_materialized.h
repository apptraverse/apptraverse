#ifndef APPTRAVERSE_UI_MATERIALIZED_H_
#define APPTRAVERSE_UI_MATERIALIZED_H_

#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/publication_channel.h"

namespace apptraverse {

void SerializeMaterializedObject(ae::Obj const& object, ByteSink& out);

void DeserializeMaterializedObject(ae::Obj& object, ByteSource& in,
                                   ae::Domain& ui_domain);

}  // namespace apptraverse

#endif  // APPTRAVERSE_UI_MATERIALIZED_H_
