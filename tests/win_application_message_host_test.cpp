#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_application_message_host.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

void TestNotifyWindowIsMessageOnly() {
  windows::WinApplicationMessageHost host([] {}, [] {}, [] {});
  HWND const hwnd = host.notify_hwnd();
  CHECK(hwnd != nullptr);
  CHECK(GetParent(hwnd) == nullptr);
  CHECK(GetWindow(hwnd, GW_OWNER) == nullptr);
}

void TestPublicationCallbacksOnGuiThread() {
  std::thread::id const gui_thread_id = std::this_thread::get_id();
  std::atomic<int> initial_count{0};
  std::atomic<int> incremental_count{0};
  std::atomic<bool> initial_on_gui{false};
  std::atomic<bool> incremental_on_gui{false};
  std::promise<void> initial_seen;
  std::promise<void> incremental_seen;
  std::future<void> initial_future = initial_seen.get_future();
  std::future<void> incremental_future = incremental_seen.get_future();
  struct ReleaseGate {
    std::mutex mu;
    std::condition_variable cv;
    bool released{false};
  };
  auto release_gate = std::make_shared<ReleaseGate>();

  windows::WinApplicationMessageHost host(
      [&] {
        ++initial_count;
        initial_on_gui = (std::this_thread::get_id() == gui_thread_id);
        initial_seen.set_value();
      },
      [&] {
        ++incremental_count;
        incremental_on_gui = (std::this_thread::get_id() == gui_thread_id);
        incremental_seen.set_value();
      },
      [] {});

  host.StartModelThread([release_gate, &host] {
    host.NotifyPublished(ApplicationPublicationKind::Initial);
    host.NotifyPublished(ApplicationPublicationKind::Incremental);
    std::unique_lock lock{release_gate->mu};
    release_gate->cv.wait(lock, [&] { return release_gate->released; });
  });

  std::thread releaser([&] {
    initial_future.wait();
    incremental_future.wait();
    {
      std::lock_guard lock{release_gate->mu};
      release_gate->released = true;
    }
    release_gate->cv.notify_all();
  });

  host.PumpUntilModelDone([] {});

  releaser.join();
  host.JoinModelThread();

  CHECK(initial_count.load() == 1);
  CHECK(incremental_count.load() == 1);
  CHECK(initial_on_gui.load());
  CHECK(incremental_on_gui.load());
}

void TestWmCloseInvokesStopOnGuiThread() {
  std::thread::id const gui_thread_id = std::this_thread::get_id();
  std::promise<void> stop_seen;
  std::future<void> stop_seen_future = stop_seen.get_future();
  std::atomic<bool> stop_on_gui{false};
  std::promise<void> model_started;
  std::future<void> model_started_future = model_started.get_future();
  struct ReleaseGate {
    std::mutex mu;
    std::condition_variable cv;
    bool released{false};
  };
  auto release_gate = std::make_shared<ReleaseGate>();

  windows::WinApplicationMessageHost host(
      [] {}, [] {},
      [&] {
        stop_on_gui = (std::this_thread::get_id() == gui_thread_id);
        stop_seen.set_value();
      });

  HWND const notify_hwnd = host.notify_hwnd();

  host.StartModelThread([release_gate, &model_started] {
    model_started.set_value();
    std::unique_lock lock{release_gate->mu};
    release_gate->cv.wait(lock, [&] { return release_gate->released; });
  });

  std::thread helper([&] {
    model_started_future.wait();
    if (PostMessageW(notify_hwnd, WM_CLOSE, 0, 0) == 0) {
      std::cerr << "PostMessageW WM_CLOSE failed\n";
      std::exit(1);
    }
    stop_seen_future.wait();
    {
      std::lock_guard lock{release_gate->mu};
      release_gate->released = true;
    }
    release_gate->cv.notify_all();
  });

  host.PumpUntilModelDone([] {});

  helper.join();
  host.JoinModelThread();
  CHECK(stop_on_gui.load());
}


struct KeyboardFixture {
  HWND control{nullptr};
  int clicks{0};
  std::promise<void> finished;
};
constexpr UINT kKeyboardDrain = WM_APP + 51;
constexpr UINT kKeyboardFinished = WM_APP + 52;
LRESULT CALLBACK KeyboardFixtureProc(HWND window, UINT message, WPARAM wparam,
                                    LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* creation = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
  }
  auto* fixture = reinterpret_cast<KeyboardFixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  if (fixture != nullptr) {
    if (message == WM_COMMAND) {
      if (LOWORD(wparam) == 73 && HIWORD(wparam) == BN_CLICKED &&
          reinterpret_cast<HWND>(lparam) == fixture->control) ++fixture->clicks;
      return 0;
    }
    if (message == WM_DRAWITEM) return TRUE;
    if (message == kKeyboardDrain) {
      // TranslateMessage appends WM_CHAR after the already queued sentinel.
      // A second boundary observes those native character messages as well.
      CHECK(PostMessageW(window, kKeyboardFinished, 0, 0) != 0);
      return 0;
    }
    if (message == kKeyboardFinished) {
      fixture->finished.set_value();
      return 0;
    }
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void RunNativeEnterFixture(wchar_t const* native_class, DWORD control_style,
                           int expected_clicks, bool expect_newline = false) {
  static ATOM const registration = [] {
    WNDCLASSW definition{};
    definition.lpfnWndProc = KeyboardFixtureProc;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = L"AppTraverseNativeEnterRegression";
    return RegisterClassW(&definition);
  }();
  CHECK(registration != 0);
  KeyboardFixture fixture;
  auto finished = fixture.finished.get_future();
  windows::WinApplicationMessageHost host([] {}, [] {}, [] {});
  HWND const window = CreateWindowExW(0, L"AppTraverseNativeEnterRegression",
      L"Keyboard regression", WS_OVERLAPPEDWINDOW, 0, 0, 220, 130, nullptr,
      nullptr, GetModuleHandleW(nullptr), &fixture);
  CHECK(window != nullptr);
  fixture.control = CreateWindowExW(0, native_class, L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | control_style, 10, 10, 180, 55,
      window, reinterpret_cast<HMENU>(73), GetModuleHandleW(nullptr), nullptr);
  CHECK(fixture.control != nullptr);
  ShowWindow(window, SW_SHOWNOACTIVATE);
  SetActiveWindow(window);
  SetFocus(fixture.control);
  CHECK(GetFocus() == fixture.control);
  // Native auto-radio focus may itself select/notify before keyboard input.
  // Isolate the Enter path from that standard focus behavior.
  fixture.clicks = 0;
  bool const check_control = control_style == BS_AUTOCHECKBOX || control_style == BS_AUTORADIOBUTTON;
  LRESULT const initial_check = check_control ? SendMessageW(fixture.control, BM_GETCHECK, 0, 0) : 0;
  CHECK(PostMessageW(fixture.control, WM_KEYDOWN, VK_RETURN, 1) != 0);
  CHECK(PostMessageW(fixture.control, WM_KEYUP, VK_RETURN, 0xC0000001) != 0);
  CHECK(PostMessageW(window, kKeyboardDrain, 0, 0) != 0);
  host.StartModelThread([&] { finished.wait(); });
  host.PumpUntilModelDone([] {});
  host.JoinModelThread();
  CHECK(fixture.clicks == expected_clicks);
  if (expect_newline) {
    wchar_t text[16]{};
    GetWindowTextW(fixture.control, text, 16);
    CHECK(wcscmp(text, L"\r\n") == 0);
  } else if (check_control) {
    CHECK(SendMessageW(fixture.control, BM_GETCHECK, 0, 0) == initial_check);
  }
  CHECK(DestroyWindow(window) != 0);
}

void TestEnterUsesNativePushButtonCommandExactlyOnce() {
  RunNativeEnterFixture(L"BUTTON", BS_PUSHBUTTON, 1);
  RunNativeEnterFixture(L"BUTTON", BS_DEFPUSHBUTTON, 1);
  RunNativeEnterFixture(L"BUTTON", BS_OWNERDRAW, 1);
  RunNativeEnterFixture(L"BUTTON", BS_AUTOCHECKBOX, 0);
  RunNativeEnterFixture(L"BUTTON", BS_AUTORADIOBUTTON, 0);
  RunNativeEnterFixture(L"EDIT", ES_MULTILINE | ES_WANTRETURN, 0, true);
}

void TestModelThreadAndDestructorLifecycle() {
  std::atomic<bool> model_ran{false};
  {
    windows::WinApplicationMessageHost host([] {}, [] {}, [] {});
    host.StartModelThread([&] { model_ran = true; });
    host.PumpUntilModelDone([] {});
    host.JoinModelThread();
    CHECK(model_ran.load());
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestNotifyWindowIsMessageOnly();
  apptraverse::test::TestPublicationCallbacksOnGuiThread();
  apptraverse::test::TestWmCloseInvokesStopOnGuiThread();
  apptraverse::test::TestModelThreadAndDestructorLifecycle();
  apptraverse::test::TestEnterUsesNativePushButtonCommandExactlyOnce();
  std::cout << "win_application_message_host_test OK\n";
  return 0;
}
