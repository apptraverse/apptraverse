#ifndef APPTRAVERSE_SURFACES_LINUX_MESSAGES_H_
#define APPTRAVERSE_SURFACES_LINUX_MESSAGES_H_

#include <cstdint>

namespace apptraverse {

inline constexpr std::uint8_t kLinuxWakeInitialPublished = 1;
inline constexpr std::uint8_t kLinuxWakeIncrementalPublished = 2;
inline constexpr std::uint8_t kLinuxWakeStop = 3;
inline constexpr std::uint8_t kLinuxWakeModelFinished = 4;

inline constexpr std::uint32_t kSurfaceAddButtonId = 1001;
inline constexpr std::uint32_t kSurfaceCloseButtonId = 1002;

// BN_CLICKED-equivalent for Presenter::OnCommand notification_code.
inline constexpr std::uint16_t kLinuxButtonClicked = 0;

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LINUX_MESSAGES_H_
