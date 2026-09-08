#ifndef APPTRAVERSE_MAIN_WINDOW_IDS_H_
#define APPTRAVERSE_MAIN_WINDOW_IDS_H_

#include <cstdint>

#include "aether-objects/obj/obj_id.h"

namespace apptraverse::main_window {

enum class ObjId : ae::ObjId::Type {
  Application = 1,
  MainWindow = 2,
};

inline constexpr ae::ObjId::Type ToObjId(ObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

inline constexpr std::int32_t kDefaultX = 80;
inline constexpr std::int32_t kDefaultY = 80;
inline constexpr std::int32_t kDefaultWidth = 800;
inline constexpr std::int32_t kDefaultHeight = 600;
inline constexpr std::int32_t kDefaultDpi = 96;

}  // namespace apptraverse::main_window

#endif  // APPTRAVERSE_MAIN_WINDOW_IDS_H_
