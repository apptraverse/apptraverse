#include "apptraverse/ui_materialized.h"

#include <cassert>

#include "apptraverse/materialized_ops.h"

namespace apptraverse {

void SerializeMaterializedObject(ae::Obj const& object, ByteSink& out) {
  auto const* ops = FindMaterializedOps(object.GetClassId());
  assert(ops != nullptr);
  assert(ops->serialize != nullptr);
  ops->serialize(object, out);
}

void DeserializeMaterializedObject(ae::Obj& object, ByteSource& in,
                                   ae::Domain& ui_domain) {
  auto const* ops = FindMaterializedOps(object.GetClassId());
  assert(ops != nullptr);
  assert(ops->deserialize != nullptr);
  ops->deserialize(object, in, ui_domain);
}

}  // namespace apptraverse
