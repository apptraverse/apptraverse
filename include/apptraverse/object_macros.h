#ifndef APPTRAVERSE_OBJECT_MACROS_H_
#define APPTRAVERSE_OBJECT_MACROS_H_

#include "aether-objects/obj/obj.h"

// CLASS_NAME is the serialized ClassId string (CRC32). Generic App Traverse
// types use APPTRAVERSE_OBJECT, which prefixes "apptraverse::". Application
// types must pass their own fully-qualified name via APPTRAVERSE_NAMED_OBJECT
// so they never collide with library types.
//
// Registrar is NOT created inline: Clang instantiates Create/Load/Save during
// static Registrar construction, which requires reflected ObjPtr member types
// to be complete. Registration lives in .cpp translation units instead.
#define APPTRAVERSE_NAMED_OBJECT(CLASS_NAME, DERIVED, BASE, VERSION)     \
 protected:                                                              \
  friend class ::ae::Registrar<DERIVED>;                                 \
  friend ae::Ptr<DERIVED> ae::MakePtr<DERIVED>();                        \
                                                                         \
 public:                                                                 \
  _AE_OBJECT_FIELDS(crc32::from_literal(CLASS_NAME).value, BASE::kClassId, \
                    VERSION)                                             \
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

#define APPTRAVERSE_OBJECT(DERIVED, BASE, VERSION)                       \
  APPTRAVERSE_NAMED_OBJECT("apptraverse::" #DERIVED, DERIVED, BASE, VERSION)

#define APPTRAVERSE_REGISTER(DERIVED)                                    \
  static ::ae::Registrar<DERIVED> g_apptraverse_registrar_##DERIVED{     \
      DERIVED::kClassId, DERIVED::kBaseClassId}

namespace apptraverse {

// Call once from each executable/shared library that links apptraverse so the
// Registrar static initializers in the static library are not stripped.
void EnsureObjectRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_MACROS_H_
