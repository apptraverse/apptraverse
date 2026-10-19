#include "apptraverse/platform/windows/surfaces_win32_z_order.h"

#include <atomic>
#include <cwchar>
#include <vector>

#include "apptraverse/platform/windows/surfaces_win_presenters.h"

namespace apptraverse {
namespace {

std::atomic<bool> g_z_order_restore_in_progress{false};

bool IsSurfaceHostWindow(DWORD process_id, HWND hwnd) {
  if (hwnd == nullptr || IsWindowVisible(hwnd) == 0) {
    return false;
  }
  DWORD window_pid = 0;
  GetWindowThreadProcessId(hwnd, &window_pid);
  if (window_pid != process_id) {
    return false;
  }
  wchar_t name[256]{};
  if (GetClassNameW(hwnd, name, 256) <= 0 ||
      wcscmp(name, kSurfacesWindowClass) != 0) {
    return false;
  }
  return true;
}

}  // namespace

void SetSurfacesWin32ZOrderRestoreInProgress(bool in_progress) {
  g_z_order_restore_in_progress.store(in_progress, std::memory_order_release);
}

bool SurfacesWin32ZOrderRestoreInProgress() {
  return g_z_order_restore_in_progress.load(std::memory_order_acquire);
}

std::vector<HWND> EnumerateSurfaceHostWindowsTopToBottom(DWORD process_id) {
  struct Ctx {
    DWORD process_id;
    std::vector<HWND> ordered;
  } ctx{process_id, {}};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        if (!IsSurfaceHostWindow(c->process_id, hwnd)) {
          return TRUE;
        }
        c->ordered.push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.ordered;
}

std::uint32_t SurfaceNumberFromHostWindowTitle(HWND hwnd) {
  wchar_t title[256]{};
  if (GetWindowTextW(hwnd, title, 256) <= 0) {
    return 0;
  }
  unsigned long number = 0;
  if (swscanf_s(title, L"Surface %lu", &number) != 1) {
    return 0;
  }
  return static_cast<std::uint32_t>(number);
}

void ApplySurfaceHostWindowsZOrderTopToBottom(
    std::vector<HWND> const& top_to_bottom) {
  if (top_to_bottom.empty()) {
    return;
  }
  for (auto it = top_to_bottom.rbegin(); it != top_to_bottom.rend(); ++it) {
    if (*it == nullptr) {
      continue;
    }
    SetWindowPos(*it, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

std::vector<std::uint32_t> SurfaceNumbersFromHostWindowsTopToBottom(
    DWORD process_id) {
  std::vector<std::uint32_t> numbers;
  for (HWND hwnd : EnumerateSurfaceHostWindowsTopToBottom(process_id)) {
    std::uint32_t const number = SurfaceNumberFromHostWindowTitle(hwnd);
    if (number != 0) {
      numbers.push_back(number);
    }
  }
  return numbers;
}

}  // namespace apptraverse
