#ifndef APPTRAVERSE_DEMO_IDS_H_
#define APPTRAVERSE_DEMO_IDS_H_

#include <chrono>

#include "aether/obj/obj_id.h"

namespace apptraverse::demo {

enum class DemoObjId : ae::ObjId::Type {
  Application = 100000,
  WindowABase = 100001,
  WindowA = 100002,
  WindowBBase = 100003,
  WindowB = 100004,
  TextToolbarBase = 100005,
  TextToolbar = 100006,
  ColorToolbarBase = 100007,
  ColorToolbar = 100008,
  ChatBase = 100009,
  Chat = 100010,
  ToolbarText = 100011,
};

constexpr ae::ObjId::Type ToObjId(DemoObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

inline constexpr char const* kToolbarTextBytes = "TextToolbar";
inline constexpr int kTextToolbarHeight = 28;
inline constexpr int kColorToolbarHeight = 28;
inline constexpr auto kModelUpdatePeriod = std::chrono::milliseconds{16};
inline constexpr auto kColorChangePeriod = std::chrono::seconds{1};
inline constexpr auto kWindowAPaintPeriod = std::chrono::seconds{1};

inline constexpr int kWindowALeft = 40;
inline constexpr int kWindowATop = 40;
inline constexpr int kWindowARight = 480;
inline constexpr int kWindowABottom = 360;
inline constexpr int kWindowBLeft = 520;
inline constexpr int kWindowBTop = 40;
inline constexpr int kWindowBRight = 1240;
inline constexpr int kWindowBBottom = 600;
inline constexpr int kDefaultDpi = 96;

}  // namespace apptraverse::demo

#endif  // APPTRAVERSE_DEMO_IDS_H_
