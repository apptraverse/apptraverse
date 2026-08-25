#ifndef APPTRAVERSE_IMMUTABLE_OBJECT_STORE_H_
#define APPTRAVERSE_IMMUTABLE_OBJECT_STORE_H_

#include <cassert>
#include <cstdint>
#include <unordered_map>

#include "aether/obj/obj_id.h"

#include "demo_model.h"

namespace apptraverse {

// Process-wide read-only constants. Both threads may resolve ConstObjectId
// to the same ImmutableString instance. Never mutated after bootstrap.
class ImmutableObjectStore {
 public:
  void Add(ImmutableString const& object) {
    assert(object.obj_id.is_valid());
    auto const id = object.obj_id.id();
    auto const* ptr = &object;
    auto [it, inserted] = by_id_.emplace(id, ptr);
    assert(inserted || it->second == ptr);
  }

  ImmutableString const* Find(ae::ObjId id) const {
    auto it = by_id_.find(id.id());
    if (it == by_id_.end()) {
      return nullptr;
    }
    return it->second;
  }

  ImmutableString const* Find(std::uint32_t id) const {
    auto it = by_id_.find(id);
    if (it == by_id_.end()) {
      return nullptr;
    }
    return it->second;
  }

  std::size_t size() const { return by_id_.size(); }

 private:
  std::unordered_map<std::uint32_t, ImmutableString const*> by_id_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_IMMUTABLE_OBJECT_STORE_H_
