#include "apptraverse/ui_materialized.h"

#include <cassert>

#include "aether/obj/registry.h"

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

ae::Ptr<ae::Obj> EnsureUiObject(ae::Domain& ui_domain, ae::ObjId id,
                                std::uint32_t class_id) {
  if (auto existing = ui_domain.Find(id)) {
    return existing;
  }
  auto* factory = ae::Registry::GetRegistry().FindFactory(class_id);
  assert(factory != nullptr);
  assert(factory->create != nullptr);
  ae::Ptr<ae::Obj> raw = factory->create();
  ui_domain.AddObject(id, raw);
  raw->domain = &ui_domain;
  raw->obj_id = id;
  return raw;
}

}  // namespace apptraverse
