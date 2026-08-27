#ifndef APPTRAVERSE_DEMO_IDS_H_
#define APPTRAVERSE_DEMO_IDS_H_

#include <chrono>

#include "aether/obj/obj_id.h"

namespace apptraverse::demo {

enum class DemoObjId : ae::ObjId::Type {
  Application = 100000,
  PaintWindow = 100002,
  LayoutWindow = 100004,
  TextToolbar = 100006,
  ColorToolbar = 100008,
  CenterStrip = 100010,
  ToolbarText = 100011,
  WinPresentationApplication = 200000,
  WinPaintWindowPresenter = 200001,
  WinLayoutWindowPresenter = 200002,
  WinTextToolbarPresenter = 200003,
  WinColorToolbarPresenter = 200004,
};

constexpr ae::ObjId::Type ToObjId(DemoObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

inline constexpr char const* kToolbarTextBytes = "TextToolbar";
inline constexpr std::int32_t kTextToolbarHeight = 28;
inline constexpr std::int32_t kColorToolbarHeight = 28;
inline constexpr auto kModelUpdatePeriod = std::chrono::milliseconds{16};
inline constexpr auto kColorChangePeriod = std::chrono::seconds{1};
inline constexpr auto kWindowAPaintPeriod = std::chrono::seconds{1};

inline constexpr std::int32_t kWindowALeft = 40;
inline constexpr std::int32_t kWindowATop = 40;
inline constexpr std::int32_t kWindowARight = 480;
inline constexpr std::int32_t kWindowABottom = 360;
inline constexpr std::int32_t kWindowBLeft = 520;
inline constexpr std::int32_t kWindowBTop = 40;
inline constexpr std::int32_t kWindowBRight = 1240;
inline constexpr std::int32_t kWindowBBottom = 600;

inline constexpr std::uint32_t kCenterStripFill = 0x00C4A870;

}  // namespace apptraverse::demo

#endif  // APPTRAVERSE_DEMO_IDS_H_
