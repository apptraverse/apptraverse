#ifndef APPTRAVERSE_SURFACES_WIN32_MESSAGES_H_
#define APPTRAVERSE_SURFACES_WIN32_MESSAGES_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse {

inline constexpr UINT WM_APPTRAVERSE_INITIAL_PUBLISHED = WM_APP + 1;
inline constexpr UINT WM_APPTRAVERSE_INCREMENTAL_PUBLISHED = WM_APP + 2;
inline constexpr UINT WM_APPTRAVERSE_STOP = WM_APP + 3;

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN32_MESSAGES_H_
