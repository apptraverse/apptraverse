#ifndef APPTRAVERSE_REMAP_POINTERS_H_
#define APPTRAVERSE_REMAP_POINTERS_H_

#include <map>
#include <type_traits>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether-objects/obj/registry.h"
#include "apptraverse/object_link.h"

namespace apptraverse::detail {

template <typename U>
void RemapField(ae::ObjPtr<U>& ptr, ae::Domain* target_domain,
                std::map<ae::ObjId, ae::ObjId> const& mapping) {
  if (ptr.is_valid()) {
    auto const it = mapping.find(ptr.id());
    if (it != mapping.end()) {
      ptr = ae::ObjPtr<U>{target_domain, it->second, ptr.flags(), ptr.cached()};
    }
  }
}

template <typename U>
void RemapField(SharedPtr<U>& ptr, ae::Domain* target_domain,
                std::map<ae::ObjId, ae::ObjId> const& mapping) {
  RemapField(ptr.as_obj_ptr(), target_domain, mapping);
}

template <typename U>
void RemapField(LocalPtr<U>& ptr, ae::Domain*,
                std::map<ae::ObjId, ae::ObjId> const&) {
  ptr.Reset();
}

template <typename U>
void RemapField(std::vector<ae::ObjPtr<U>>& vec, ae::Domain* target_domain,
                std::map<ae::ObjId, ae::ObjId> const& mapping) {
  for (auto& item : vec) {
    RemapField(item, target_domain, mapping);
  }
}

template <typename U>
void RemapField(std::vector<SharedPtr<U>>& vec, ae::Domain* target_domain,
                std::map<ae::ObjId, ae::ObjId> const& mapping) {
  for (auto& item : vec) {
    RemapField(item, target_domain, mapping);
  }
}

template <typename U>
void RemapField(std::vector<LocalPtr<U>>& vec, ae::Domain*,
                std::map<ae::ObjId, ae::ObjId> const&) {
  for (auto& item : vec) {
    item.Reset();
  }
}

inline void RemapField(auto&, ae::Domain*,
                       std::map<ae::ObjId, ae::ObjId> const&) {
  // Ordinary scalars and base references: preserved unchanged!
}

template <typename T>
  requires(ae::reflect::Reflectable<T>)
void RemapReflectedPointers(
    T& obj, ae::Domain* target_domain,
    std::map<ae::ObjId, ae::ObjId> const& mapping) {
  auto reflection = ae::reflect::make_reflection(obj);
  reflection.Apply([&](auto&&... fields) {
    (RemapField(fields, target_domain, mapping), ...);
  });
}

template <typename U>
bool ValidateFieldPointer(ae::ObjPtr<U> const& ptr,
                          ae::RamDomainStorage const& storage) {
  if (ptr.is_valid()) {
    auto const it = storage.state.find(ptr.id());
    if (it == storage.state.end() || !it->second.has_value() ||
        !ptr.is_loaded()) {
      return false;
    }
    if (ptr.cached()) {
      if (ae::Registry::GetRegistry().GenerationDistance(
              U::kClassId, ptr.cached()->GetClassId()) < 0) {
        return false;
      }
    }
  }
  return true;
}

template <typename U>
bool ValidateFieldPointer(SharedPtr<U> const& ptr,
                          ae::RamDomainStorage const& storage) {
  return ValidateFieldPointer(ptr.as_obj_ptr(), storage);
}

template <typename U>
bool ValidateFieldPointer(LocalPtr<U> const&, ae::RamDomainStorage const&) {
  return true;
}

template <typename U>
bool ValidateFieldPointer(std::vector<ae::ObjPtr<U>> const& vec,
                          ae::RamDomainStorage const& storage) {
  for (auto const& item : vec) {
    if (!ValidateFieldPointer(item, storage)) {
      return false;
    }
  }
  return true;
}

template <typename U>
bool ValidateFieldPointer(std::vector<SharedPtr<U>> const& vec,
                          ae::RamDomainStorage const& storage) {
  for (auto const& item : vec) {
    if (!ValidateFieldPointer(item, storage)) {
      return false;
    }
  }
  return true;
}

template <typename U>
bool ValidateFieldPointer(std::vector<LocalPtr<U>> const&,
                          ae::RamDomainStorage const&) {
  return true;
}

inline bool ValidateFieldPointer(auto const&, ae::RamDomainStorage const&) {
  return true;
}

template <typename T>
  requires(ae::reflect::Reflectable<T>)
bool ValidateReflectedPointers(T const& obj,
                               ae::RamDomainStorage const& storage) {
  bool ok = true;
  auto reflection = ae::reflect::make_reflection(obj);
  reflection.Apply([&](auto const&... fields) {
    ((ok = ok && ValidateFieldPointer(fields, storage)), ...);
  });
  return ok;
}

}  // namespace apptraverse::detail

#endif  // APPTRAVERSE_REMAP_POINTERS_H_
