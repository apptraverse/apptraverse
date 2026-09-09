#ifndef APPTRAVERSE_DYNAMIC_WIN32_MESSAGES_H_
#define APPTRAVERSE_DYNAMIC_WIN32_MESSAGES_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse {

inline constexpr UINT WM_APPTRAVERSE_INITIAL_PUBLISHED = WM_APP + 1;
inline constexpr UINT WM_APPTRAVERSE_INCREMENTAL_PUBLISHED = WM_APP + 2;
inline constexpr UINT WM_APPTRAVERSE_STOP = WM_APP + 3;
// wparam = closing MainWindow ObjId. Application decides whether to stop.
inline constexpr UINT WM_APPTRAVERSE_CLOSE_WINDOW = WM_APP + 4;

}  // namespace apptraverse

#endif  // APPTRAVERSE_DYNAMIC_WIN32_MESSAGES_H_
