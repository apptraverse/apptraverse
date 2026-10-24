#ifndef APPTRAVERSE_TESTS_PREFAB_PTR_H_
#define APPTRAVERSE_TESTS_PREFAB_PTR_H_

#include <cstdlib>

#include "aether-miscpp/domain_visitor/domain_visitor.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether-objects/ptr/ptr.h"

namespace apptraverse::test {

// Test/fixture-only wire: canonical owner of instance ObjId → prefab ObjId.
// Wire order: prefab_id, then instance ObjPtr (lazy via ObjPtr flags).
template <typename T>
class PrefabPtr {
 public:
  PrefabPtr() = default;

  static PrefabPtr FromInstance(ae::ObjPtr<T> instance, ae::ObjId prefab_id) {
    if (!instance.is_valid() || !prefab_id.is_valid()) {
      std::abort();
    }
    PrefabPtr out;
    out.instance_ = std::move(instance);
    out.prefab_id_ = prefab_id;
    return out;
  }

  ae::ObjId instance_id() const { return instance_.id(); }
  ae::ObjId prefab_id() const { return prefab_id_; }
  ae::ObjId id() const { return instance_.id(); }

  bool is_valid() const { return instance_.is_valid(); }
  bool is_loaded() const { return instance_.is_loaded(); }

  ae::Ptr<T> Load() const { return instance_.Load(); }

  ae::ObjPtr<T> const& instance_ptr() const { return instance_; }
  ae::ObjPtr<T>& instance_ptr() { return instance_; }

  operator ae::ObjPtr<T> const&() const { return instance_; }

  ae::ObjId prefab_id_{};
  ae::ObjPtr<T> instance_{};
};

}  // namespace apptraverse::test

namespace ae::seri {

template <typename T>
struct Serializer<BinaryArchive<DomainBuffer>,
                  apptraverse::test::PrefabPtr<T>> {
  using Archive = BinaryArchive<DomainBuffer>;
  using PrefabPtr = apptraverse::test::PrefabPtr<T>;

  SeriResult Seri(Archive& archive, Meta<PrefabPtr const> meta) const {
    TRY_RESULT(archive.Save(Meta{meta.value.prefab_id_}));
    return archive.Save(Meta{meta.value.instance_});
  }

  SeriResult Deseri(Archive& archive, Meta<PrefabPtr> meta) const {
    TRY_RESULT(archive.Load(Meta{meta.value.prefab_id_}));
    return archive.Load(Meta{meta.value.instance_});
  }
};

}  // namespace ae::seri

namespace ae::domain_visitor {

template <typename T>
struct NodeVisitor<apptraverse::test::PrefabPtr<T>>
    : NodeVisitor<ae::ObjPtr<T>> {};

}  // namespace ae::domain_visitor

#endif  // APPTRAVERSE_TESTS_PREFAB_PTR_H_
