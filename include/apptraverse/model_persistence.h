#ifndef APPTRAVERSE_MODEL_PERSISTENCE_H_
#define APPTRAVERSE_MODEL_PERSISTENCE_H_

#include <cstdint>
#include <vector>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

namespace apptraverse {

// Objects registered here participate in the live model and UI publication,
// but are omitted from persistent snapshots (RAM or directory).
void RegisterRuntimeOnlyClassId(std::uint32_t class_id);
bool IsRuntimeOnlyClassId(std::uint32_t class_id);

// Presenters and runtime-only classes are excluded from durable checkpoints.
bool IsExcludedFromPersistentSnapshot(ae::Obj const& object);

// SerializePersistentModelSnapshot / LoadPersistentModelSnapshot /
// SavePersistentModelSnapshotToStorage are declared in object_serialization.h.

}  // namespace apptraverse

#endif  // APPTRAVERSE_MODEL_PERSISTENCE_H_
