#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "aether/clock.h"
#include "aether/domain_storage/file_system_std_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/application_ids.h"
#include "apptraverse/app.h"

#include "../common/graph_builder.h"
#include "display_environment_changed_event.h"
#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "window_bounds_changed_event.h"
#include "windows_window.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(WindowsWindow);
APPTRAVERSE_REGISTER(WinWindowPresenter);
APPTRAVERSE_REGISTER(WinChatPresenter);
APPTRAVERSE_REGISTER(WindowBoundsChangedEvent);
APPTRAVERSE_REGISTER(DisplayEnvironmentChangedEvent);

}  // namespace
}  // namespace apptraverse

namespace {

bool IsDistillMode(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--distill") == 0) {
      return true;
    }
  }
  return false;
}

void Distill() {
  std::filesystem::remove_all("state");

  ae::FileSystemStdStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto graph =
      apptraverse::examples::BuildSingleClientChatGraph<
          apptraverse::WindowsWindow, apptraverse::WinWindowPresenter,
          apptraverse::WinChatPresenter>(domain);

  auto window = graph.window;
  window.Load();
  auto& win = static_cast<apptraverse::WindowsWindow&>(*window);
  win.x = 120;
  win.y = 80;
  win.width = 720;
  win.height = 520;

  graph.app.Save();
  std::cout << "Distilled single-client chat graph to ./state\n";
}

int Run() {
  ae::FileSystemStdStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto app = apptraverse::App::ptr::Declare(ae::CreateWith{domain}.with_id(
      apptraverse::ToObjId(apptraverse::ApplicationObjId::Application)));
  app.Load();
  if (!app.is_loaded()) {
    std::cerr << "Failed to load App. Run with --distill first.\n";
    return 1;
  }

  auto window = app->window;
  window.Load();
  auto presenter = window->presenter;
  presenter.Load();

  if (presenter->GetClassId() != apptraverse::WinWindowPresenter::kClassId) {
    std::cerr << "Expected WinWindowPresenter after load\n";
    return 1;
  }

  auto& win_presenter =
      static_cast<apptraverse::WinWindowPresenter&>(*presenter);
  win_presenter.CreateNativeWindow();

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace

int main(int argc, char** argv) {
  apptraverse::EnsureObjectRegistration();
  if (IsDistillMode(argc, argv)) {
    Distill();
    return 0;
  }
  return Run();
}
