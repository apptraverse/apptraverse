#ifndef APPTRAVERSE_TESTS_PREFAB_RELEASE_IDS_H_
#define APPTRAVERSE_TESTS_PREFAB_RELEASE_IDS_H_

#include "aether-objects/obj/obj_id.h"

namespace apptraverse::test::prefab_release {

inline constexpr ae::ObjId::Type kMainWindowPrefabId = 60001;
inline constexpr ae::ObjId::Type kToolWindowPrefabId = 60002;
inline constexpr ae::ObjId::Type kMainLabelConstId = 60011;
inline constexpr ae::ObjId::Type kToolLabelConstId = 60012;

inline constexpr ae::ObjId::Type kWorkspaceId = 61000;
inline constexpr ae::ObjId::Type kWindowAId = 61001;
inline constexpr ae::ObjId::Type kWindowBId = 61002;
inline constexpr ae::ObjId::Type kWindowCId = 61003;
inline constexpr ae::ObjId::Type kNullPresenterId = 61010;
inline constexpr ae::ObjId::Type kLayoutId = 61020;
inline constexpr ae::ObjId::Type kCounterId = 61021;
inline constexpr ae::ObjId::Type kDetailsId = 61022;
inline constexpr ae::ObjId::Type kHistoryId = 61023;
inline constexpr ae::ObjId::Type kPreferencesId = 61030;

}  // namespace apptraverse::test::prefab_release

#endif  // APPTRAVERSE_TESTS_PREFAB_RELEASE_IDS_H_
