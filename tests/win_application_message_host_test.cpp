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
  std::cout << "win_application_message_host_test OK\n";
  return 0;
}
