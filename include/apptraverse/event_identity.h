#ifndef APPTRAVERSE_EVENT_IDENTITY_H_
#define APPTRAVERSE_EVENT_IDENTITY_H_

#include <cstdint>

#include "aether/obj/obj_id.h"
#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

struct EventIdentity {
  ae::ObjId origin;
  std::uint32_t sequence{0};

  bool IsValid() const { return origin.IsValid() && sequence != 0; }

  bool operator==(EventIdentity const&) const = default;

  AE_REFLECT_MEMBERS(origin, sequence)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_IDENTITY_H_
