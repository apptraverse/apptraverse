#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <array>
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_application_message_host.h"
#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_text_control_presenter.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "chat_windows_presenter.h"
#include "chat_controls.h"
#include "chat_ui_session.h"

#ifndef APPTRAVERSE_CHAT_TITLE
#  define APPTRAVERSE_CHAT_TITLE L"SharedNode Chat"
#endif

namespace apptraverse::chat {
namespace {
using ui::windows::Utf8ToWide;
using ui::windows::WideToUtf8;
constexpr UINT kViewChanged = WM_APP + 1;

void Close(HANDLE handle) {
  if (!CloseHandle(handle)) {
    DWORD const error = GetLastError();
    FatalWin32("CloseHandle chat profile mutex", error);
  }
}

bool CopyClipboard(HWND hwnd, std::string const& utf8) {
  auto value = Utf8ToWide(utf8);
  bool copied = false;
  if (OpenClipboard(hwnd)) {
    auto size = (value.size() + 1) * sizeof(wchar_t);
    auto memory = GlobalAlloc(GMEM_MOVEABLE, size);
    auto* data = memory ? GlobalLock(memory) : nullptr;
    if (data) {
      std::memcpy(data, value.c_str(), size);
      GlobalUnlock(memory);
      copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    }
    if (!copied && memory) GlobalFree(memory);
    CloseClipboard();
  }
  return copied;
}
void InstallPresenters() {
  ui::windows::EnsureWinControlPresenterRegistration();
  EnsureChatWindowsPresenterRegistration();
  ui::windows::EnsureWinProcessDpiAwareness();
  ui::windows::WinUiEnvironment environment{nullptr};
  auto baseline = environment.snapshot();
  gui::UiFactories factories;
  factories.create_window = [](ae::Domain& d) {
    return ChatDesktopWindowPresenter::ptr::Create(ae::CreateWith{d});
  };
  factories.create_button = [](ae::Domain& d) {
    return ui::windows::WinPushButtonPresenter::ptr::Create(ae::CreateWith{d});
  };
  factories.create_text = [](ae::Domain& d) {
    return ui::windows::WinTextControlPresenter::ptr::Create(ae::CreateWith{d});
  };
  factories.create_list = [](ae::Domain& d) {
    return ui::windows::WinListContainerPresenter::ptr::Create(
        ae::CreateWith{d});
  };
  factories.prepare_device = [baseline](ui::UiDeviceState& device) {
    device.ObserveEnvironment(baseline, ui::UiEnvironmentReason::kStartup);
  };
  gui::ConfigureUiFactories(std::move(factories));
}
std::wstring ProfileMutex(std::filesystem::path const& profile) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (wchar_t c : profile.wstring()) {
    if (c >= L'A' && c <= L'Z') {
      c += L'a' - L'A';
    }
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  return L"Local\\AppTraverseSharedNodeChat-" + std::to_wstring(hash);
}
}  // namespace
}  // namespace apptraverse::chat

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  using namespace apptraverse::chat;  // Native executable entry point.
  gui::EnsureUiRegistration();
  InstallPresenters();
  int argc = 0;
  auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    DWORD const error = GetLastError();
    apptraverse::FatalWin32("CommandLineToArgvW chat", error);
  }
  bool is_host = false, role_seen = false, valid = true, load_only = false;
  std::filesystem::path distill_output;
  std::wstring name, profile_argument, host_id;
  for (int i = 1; i < argc && valid; ++i) {
    std::wstring const arg = argv[i];
    if (arg == L"--load-only") {
      load_only = true;
      continue;
    }
    if (arg == L"--distill-only" && i + 1 < argc) {
      distill_output = argv[++i];
      continue;
    }
    if (arg == L"--host" || arg == L"--client") {
      if (role_seen) {
        valid = false;
        break;
      }
      role_seen = true;
      is_host = arg == L"--host";
      if (!is_host && i + 1 < argc &&
          std::wstring{argv[i + 1]}.find(L"--") != 0)
        host_id = argv[++i];
    } else if ((arg == L"--profile" || arg == L"--name") && i + 1 < argc &&
               std::wstring{argv[i + 1]}.find(L"--") != 0) {
      (arg == L"--profile" ? profile_argument : name) = argv[++i];
    } else
      valid = false;
  }
  LocalFree(argv);
  if (!distill_output.empty()) {
#ifdef APPTRAVERSE_CHAT_UI_PRODUCER
    return gui::ExportUiSupply(distill_output);
#else
    return 1;
#endif
  }
  if (!valid) {
    MessageBoxW(
        nullptr,
        L"Usage: shared_node_chat.exe [--host | --client [HOST-AETHER-ID]] "
        L"[--profile PATH] [--name NAME]",
        L"SharedNode Chat", MB_OK | MB_ICONERROR);
    return 1;
  }
  if (name.empty()) name = is_host ? L"Host" : L"Client";
  if (profile_argument.empty())
    profile_argument =
        is_host ? L"shared-chat-host-state" : L"shared-chat-client-state";
  std::error_code error;
  auto const profile =
      std::filesystem::absolute(profile_argument, error).lexically_normal();
  if (error) {
    MessageBoxW(nullptr, L"The profile path is invalid.", L"SharedNode Chat",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  HANDLE const profile_lock =
      CreateMutexW(nullptr, FALSE, ProfileMutex(profile).c_str());
  DWORD const mutex_error = GetLastError();
  if (!profile_lock) {
    apptraverse::FatalWin32("CreateMutexW chat profile", mutex_error);
  }
  if (mutex_error == ERROR_ALREADY_EXISTS) {
    Close(profile_lock);
    MessageBoxW(nullptr,
                L"This profile is already open. Use a different --profile for "
                L"another participant.",
                L"SharedNode Chat", MB_OK | MB_ICONINFORMATION);
    return 1;
  }
  HANDLE stopped = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!stopped)
    apptraverse::FatalWin32("CreateEventW chat lifecycle", GetLastError());
  std::unique_ptr<gui::Session> session;
  apptraverse::ui::windows::WinPresentationHost presentation;
  std::uint64_t last_copy{};
  bool stopping = false;
  auto refresh = [&] {
    if (!session || stopping || !session->ApplyPublication(&presentation))
      return;
    auto const* mirror = session->Mirror();
    if (mirror->copy_request != last_copy) {
      last_copy = mirror->copy_request;
      auto copied =
          CopyClipboard(presentation.notify_hwnd, mirror->connection.uid);
      session->Proxy().Invoke(gui::RootId(), &gui::ChatUi::ClipboardResult,
                              copied);
    }
  };
  auto stop = [&] {
    if (stopping) return;
    stopping = true;
    session->Stop();
    if (!SetEvent(stopped))
      apptraverse::FatalWin32("SetEvent chat lifecycle", GetLastError());
  };
  apptraverse::windows::WinApplicationMessageHost messages{refresh, refresh,
                                                           stop};
  presentation.notify_hwnd = messages.notify_hwnd();
  presentation.opaque = &stop;
  presentation.on_desktop_window_user_close = [](void* context, ae::Obj*) {
    (*static_cast<decltype(stop)*>(context))();
  };
  session = std::make_unique<gui::Session>(
      profile, WideToUtf8(name), is_host, WideToUtf8(host_id),
      [&] {
        messages.NotifyPublished(
            apptraverse::ApplicationPublicationKind::Incremental);
      },
      load_only);
  // The common message host owns native keyboard/navigation and publication
  // wakeups. ChatSession owns the network/model worker and emits no UI timer.
  messages.StartModelThread([&] {
    session->Start();
    if (WaitForSingleObject(stopped, INFINITE) != WAIT_OBJECT_0)
      apptraverse::FatalWin32("WaitForSingleObject chat lifecycle",
                              GetLastError());
  });
  messages.PumpUntilModelDone(stop);
  messages.JoinModelThread();
  Close(stopped);
  Close(profile_lock);
  return 0;
}
