#ifndef APPTRAVERSE_SURFACES_WIN32_Z_ORDER_H_
#define APPTRAVERSE_SURFACES_WIN32_Z_ORDER_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <vector>

namespace apptraverse {

void SetSurfacesWin32ZOrderRestoreInProgress(bool in_progress);
bool SurfacesWin32ZOrderRestoreInProgress();

// Visible top-level Surface host windows for this process (top → bottom).
std::vector<HWND> EnumerateSurfaceHostWindowsTopToBottom(DWORD process_id);

// Parses "Surface N" window title; returns 0 when not matched.
std::uint32_t SurfaceNumberFromHostWindowTitle(HWND hwnd);

// Apply top → bottom order without moving or resizing (HWND_TOP chain).
void ApplySurfaceHostWindowsZOrderTopToBottom(
    std::vector<HWND> const& top_to_bottom);

std::vector<std::uint32_t> SurfaceNumbersFromHostWindowsTopToBottom(
    DWORD process_id);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN32_Z_ORDER_H_
