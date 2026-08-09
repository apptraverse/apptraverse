#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether/clock.h"
#include "aether/domain_storage/file_system_std_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/app.h"
#include "apptraverse/chat.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/client.h"
#include "apptraverse/window.h"

#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "windows_window.h"

namespace {

constexpr ae::ObjId::Type kAppId = 1;

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

  auto app = apptraverse::App::ptr::Create(ae::CreateWith{domain}.with_id(kAppId));
  auto window = apptraverse::WindowsWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 1));
  auto window_presenter = apptraverse::WinWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 2));
  auto chat_base = apptraverse::Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 3));
  auto chat = apptraverse::Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 4));
  auto chat_presenter = apptraverse::WinChatPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 5));
  auto alice = apptraverse::Client::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 6));

  alice->name = "Alice";
  window->width = 720;
  window->height = 520;

  app->window = window;
  window->presenter = window_presenter;
  window->chat = chat;
  window_presenter->window = window;
  window_presenter->chat_presenter = chat_presenter;

  chat->base = chat_base;
  chat->presenter = chat_presenter;
  chat->CaptureBaseStateForDistill();

  chat_presenter->chat = chat;
  chat_presenter->window_presenter = window_presenter;

  auto join = apptraverse::JoinClientEvent::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId + 7));
  join->client = alice;
  chat->Commit(join);

  app.Save();
  std::cout << "Distilled single-client chat graph to ./state\n";
}

int Run() {
  ae::FileSystemStdStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto app =
      apptraverse::App::ptr::Declare(ae::CreateWith{domain}.with_id(kAppId));
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
  if (IsDistillMode(argc, argv)) {
    Distill();
    return 0;
  }
  return Run();
}
