#pragma once
#include "wire.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
namespace update_lab {
struct DeletedLayer {
  ae::ObjId id;
  std::uint32_t class_id;
  std::uint8_t version;
  AE_REFLECT_MEMBERS(id, class_id, version)
};
struct DeletedClass {
  ae::ObjId id;
  std::uint32_t class_id;
  AE_REFLECT_MEMBERS(id, class_id)
};
struct StateDelta {
  ae::RamDomainStorage::ObjClassData changed;
  std::vector<DeletedLayer> removed;
  std::vector<DeletedClass> removed_classes;
  std::vector<ae::ObjId> removed_objects;
  Bytes scope_metadata;
  AE_REFLECT_MEMBERS(changed, removed, removed_classes, removed_objects, scope_metadata)
};
struct StatePackage {
  std::uint32_t kind, from, to; // full=0, delta=1, language/executable=2
  std::uint64_t raw_size;
  Bytes compressed;
  AE_REFLECT_MEMBERS(kind, from, to, raw_size, compressed)
};
Bytes Compress(Bytes const&, int level = 19);
bool Expand(Bytes const&, std::uint64_t size, Bytes&);
Bytes PackageRaw(Bytes const&, unsigned kind, unsigned from, unsigned to);
bool UnpackRaw(Bytes const&, StatePackage&, Bytes&);
Bytes FullState(ae::RamDomainStorage const&, unsigned version);
Bytes DeltaState(ae::RamDomainStorage const&, ae::RamDomainStorage const&, unsigned from, unsigned to);
bool ApplyStatePackage(Bytes const&, ae::RamDomainStorage&, unsigned& version);
}
