#ifndef APPTRAVERSE_OBJECT_MACROS_H_
#define APPTRAVERSE_OBJECT_MACROS_H_

#include "aether/obj/obj.h"

// Qualify App Traverse class names in the CRC so they never collide with
// identically named ae:: types (e.g. Client) when both are linked.
//
// Registrar is NOT created inline: Clang instantiates Create/Load/Save during
// static Registrar construction, which requires reflected ObjPtr member types
// to be complete. Registration lives in .cpp translation units instead.
#define APPTRAVERSE_OBJECT(DERIVED, BASE, VERSION)                       \
 protected:                                                              \
  friend class ::ae::Registrar<DERIVED>;                                 \
  friend ae::Ptr<DERIVED> ae::MakePtr<DERIVED>();                        \
                                                                         \
 public:                                                                 \
  _AE_OBJECT_FIELDS(                                                     \
      crc32::from_literal("apptraverse::" #DERIVED).value, BASE::kClassId, \
      VERSION)                                                           \
                                                                         \
  using Base = BASE;                                                     \
  using ptr = ::ae::ObjPtr<DERIVED>;                                     \
                                                                         \
  Base& base_{*this};                                                    \
                                                                         \
  std::uint32_t GetClassId() const override { return kClassId; }         \
                                                                         \
 private:                                                                \
  /* add rest class's staff after */

#define APPTRAVERSE_REGISTER(DERIVED)                                    \
  static ::ae::Registrar<DERIVED> g_apptraverse_registrar_##DERIVED{     \
      DERIVED::kClassId, DERIVED::kBaseClassId}

namespace apptraverse {

// Call once from each executable/shared library that links apptraverse so the
// Registrar static initializers in the static library are not stripped.
void EnsureObjectRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_MACROS_H_
