#ifndef APPTRAVERSE_TESTS_PREFAB_PTR_H_
#define APPTRAVERSE_TESTS_PREFAB_PTR_H_

#include <cstdlib>
#include <unordered_map>

#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/ptr/ptr.h"

namespace apptraverse::test {

// Test/fixture-only wire: serialization still uses ordinary ObjPtr fields on
// objects. PrefabPtr is the canonical owner of instance→prefab origin for
// prefab-backed instances; aliases remain plain ObjPtr/ObjectLink.
template <typename T>
class PrefabPtr {
 public:
  PrefabPtr() = default;

  static PrefabPtr FromInstance(ae::ObjPtr<T> instance, ae::ObjId prefab_id,
                                bool lazy_by_default = false) {
    if (!instance.is_valid() || !prefab_id.is_valid()) {
      std::abort();
    }
    RegisterOrigin(instance.id(), prefab_id);
    PrefabPtr out;
    out.instance_ = std::move(instance);
    out.prefab_id_ = prefab_id;
    out.lazy_by_default_ = lazy_by_default;
    return out;
  }

  ae::ObjId instance_id() const { return instance_.id(); }
  ae::ObjId prefab_id() const { return prefab_id_; }
  bool lazy_by_default() const { return lazy_by_default_; }

  bool is_valid() const { return instance_.is_valid(); }
  bool is_loaded() const { return instance_.is_loaded(); }

  ae::Ptr<T> Load() const { return instance_.Load(); }

  ae::ObjPtr<T> const& instance_ptr() const { return instance_; }

  static bool LookupOrigin(ae::ObjId instance_id, ae::ObjId* prefab_id_out) {
    auto const it = Origins().find(instance_id.id());
    if (it == Origins().end()) {
      return false;
    }
    if (prefab_id_out != nullptr) {
      *prefab_id_out = ae::ObjId{it->second};
    }
    return true;
  }

  static void ClearOrigins() { Origins().clear(); }

  static bool RegisterOrigin(ae::ObjId instance_id, ae::ObjId prefab_id) {
    if (!instance_id.is_valid() || !prefab_id.is_valid()) {
      return false;
    }
    auto& map = Origins();
    auto [it, inserted] = map.try_emplace(instance_id.id(), prefab_id.id());
    if (!inserted && it->second != prefab_id.id()) {
      return false;
    }
    return true;
  }

  static bool RefuseDirectPrefabInstanceLoad(ae::ObjId instance_id) {
    ae::ObjId prefab{};
    if (!LookupOrigin(instance_id, &prefab)) {
      return false;
    }
    (void)prefab;
    return true;
  }

 private:
  static std::unordered_map<ae::ObjId::Type, ae::ObjId::Type>& Origins() {
    static std::unordered_map<ae::ObjId::Type, ae::ObjId::Type> origins;
    return origins;
  }

  ae::ObjPtr<T> instance_{};
  ae::ObjId prefab_id_{};
  bool lazy_by_default_{false};
};

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_PREFAB_PTR_H_
