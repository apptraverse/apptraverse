#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../examples/shared_node_chat/chat_controls.h"

// Explicit, network-dependent acceptance test. It controls only the three
// processes that it launches, through the controls' native command
// notifications.
namespace {
using namespace apptraverse::chat;
using namespace std::chrono_literals;

struct Process;
Process* running_processes{};
std::size_t process_count{};
std::filesystem::path profiles;
[[noreturn]] void Fail(char const* message);

void Require(bool condition, char const* message) {
  if (!condition) {
    Fail(message);
  }
}

LRESULT Call(HWND hwnd, UINT message, WPARAM wparam = 0, LPARAM lparam = 0) {
  DWORD_PTR result{};
  Require(
      hwnd && SendMessageTimeoutW(hwnd, message, wparam, lparam,
                                  SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result),
      "Native control did not respond");
  return static_cast<LRESULT>(result);
}

HWND Control(HWND window, int id) {
  auto control = GetDlgItem(window, id);
  Require(control != nullptr, "Expected chat control is missing");
  return control;
}

void Click(HWND window, int id) {
  auto const control = Control(window, id);
  Require(IsWindowEnabled(control), "Requested button is disabled");
  // Cross-process BM_CLICK can be ignored by inactive windows. Dispatch the
  // button's ordinary BN_CLICKED notification to its actual parent instead.
  Call(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
       reinterpret_cast<LPARAM>(control));
}

std::wstring Text(HWND control) {
  auto const size = Call(control, WM_GETTEXTLENGTH);
  Require(size >= 0 && size < 16 * 1024 * 1024, "Invalid native text length");
  std::wstring text(static_cast<std::size_t>(size) + 1, L'\0');
  auto const copied = Call(control, WM_GETTEXT, text.size(),
                           reinterpret_cast<LPARAM>(text.data()));
  Require(copied >= 0, "Could not read native text");
  text.resize(static_cast<std::size_t>(copied));
  return text;
}

void SetText(HWND control, std::wstring const& value) {
  Require(Call(control, WM_SETTEXT, 0,
               reinterpret_cast<LPARAM>(value.c_str())) != 0,
          "Could not set native text");
}

std::vector<std::wstring> People(HWND window) {
  auto const control = Control(window, kParticipantsControl);
  auto const count = Call(control, LB_GETCOUNT);
  Require(count >= 0 && count < 100, "Invalid participant count");
  std::vector<std::wstring> rows;
  for (LRESULT index = 0; index < count; ++index) {
    auto const size = Call(control, LB_GETTEXTLEN, index);
    Require(size >= 0 && size < 4096, "Invalid participant text length");
    std::wstring row(static_cast<std::size_t>(size) + 1, L'\0');
    auto const copied =
        Call(control, LB_GETTEXT, index, reinterpret_cast<LPARAM>(row.data()));
    Require(copied >= 0, "Could not read participant text");
    row.resize(static_cast<std::size_t>(copied));
    rows.push_back(std::move(row));
  }
  return rows;
}

struct Process {
  HANDLE handle{};
  DWORD pid{};
  HWND window{};
  std::wstring name;
  std::filesystem::path profile;
  std::wstring host_id;

  Process() = default;
  Process(Process const&) = delete;
  Process& operator=(Process const&) = delete;
  ~Process() { Stop(false); }

  void Crash() {
    Require(handle && TerminateProcess(handle, 97),
            "Could not kill owned chat process");
    Require(WaitForSingleObject(handle, 5000) == WAIT_OBJECT_0,
            "Killed chat process did not exit");
    CloseHandle(handle);
    handle = nullptr;
    window = nullptr;
  }

  void Start(std::filesystem::path const& exe) {
    Require(handle == nullptr, "Process already running");
    std::error_code error;
    std::filesystem::create_directories(profile, error);
    Require(!error, "Could not create participant profile directory");
    auto command = L"\"" + exe.wstring() + L"\" --profile \"" +
                   profile.wstring() + L"\" --name \"" + name + L"\"";
    command += L" --load-only";
    command += name == L"Host" ? L" --host" : L" --client";
    if (!host_id.empty()) command += L" \"" + host_id + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    auto const log_path =
        profile / (L"process-" + std::to_wstring(GetTickCount64()) + L".log");
    HANDLE log =
        CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(log != INVALID_HANDLE_VALUE, "Could not open owned process log");
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    auto const created =
        CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                       nullptr, exe.parent_path().c_str(), &startup, &process);
    CloseHandle(log);
    Require(created != FALSE, "Could not launch shared_node_chat");
    CloseHandle(process.hThread);
    handle = process.hProcess;
    pid = process.dwProcessId;
  }

  HWND Window() {
    DWORD code{};
    Require(handle && GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE,
            "Chat process exited unexpectedly");
    if (window && IsWindow(window)) {
      return window;
    }
    struct Search {
      DWORD pid;
      HWND found{};
    } search{pid};
    EnumWindows(
        [](HWND candidate, LPARAM parameter) -> BOOL {
          auto& search = *reinterpret_cast<Search*>(parameter);
          DWORD candidate_pid{};
          GetWindowThreadProcessId(candidate, &candidate_pid);
          wchar_t type[128]{};
          if (candidate_pid == search.pid &&
              GetClassNameW(candidate, type, 128) &&
              std::wstring{type} == kWindowClass) {
            search.found = candidate;
            return FALSE;
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    window = search.found;
    return window;
  }

  void Stop(bool strict) {
    if (!handle) {
      return;
    }
    DWORD code{};
    GetExitCodeProcess(handle, &code);
    if (code == STILL_ACTIVE && window && IsWindow(window)) {
      PostMessageW(window, WM_CLOSE, 0, 0);
    }
    bool const stopped =
        WaitForSingleObject(handle, strict ? 15000 : 3000) == WAIT_OBJECT_0;
    if (!stopped) {
      TerminateProcess(handle, 1);  // Only a process created by this test.
      WaitForSingleObject(handle, 3000);
    }
    GetExitCodeProcess(handle, &code);
    CloseHandle(handle);
    handle = nullptr;
    window = nullptr;
    if (strict) {
      Require(stopped,
              "Closing the chat did not stop its model thread promptly");
      if (code != 0) {
        std::wcerr << name << L" exit code: " << code << L" (0x" << std::hex
                   << code << std::dec << L")\n";
      }
      Require(code == 0, "Chat process reported a failure on exit");
    }
  }
};

[[noreturn]] void Fail(char const* message) {
  std::cerr << "FAIL: " << message << std::endl;
  std::wcerr << L"Profiles: " << profiles.wstring() << std::endl;
  for (std::size_t index = 0; index < process_count; ++index) {
    auto& process = running_processes[index];
    if (process.window && IsWindow(process.window)) {
      wchar_t status[1024]{};
      DWORD_PTR copied{};
      auto control = GetDlgItem(process.window, kStatusControl);
      if (control &&
          SendMessageTimeoutW(control, WM_GETTEXT, 1024,
                              reinterpret_cast<LPARAM>(status),
                              SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &copied)) {
        std::wcerr << process.name << L": " << status << std::endl;
      }
    }
    process.Stop(false);
  }
  std::exit(1);
}

template <class Predicate>
void Wait(char const* stage, Predicate predicate) {
  std::cout << stage << std::endl;
  auto const deadline = std::chrono::steady_clock::now() + 180s;
  while (!predicate()) {
    Require(std::chrono::steady_clock::now() < deadline, stage);
    std::this_thread::sleep_for(100ms);
  }
}

bool Ready(Process& process) {
  auto window = process.Window();
  // EnumWindows may see the HWND while WM_CREATE is still constructing its
  // children. Missing controls are a real startup state until readiness.
  auto host = window ? GetDlgItem(window, kHostControl) : nullptr;
  return host && IsWindowEnabled(host);
}

void Send(Process& process, std::wstring const& message) {
  auto const window = process.Window();
  Require(IsWindowEnabled(Control(window, kInputControl)) != FALSE,
          "Chat input is disabled after joining");
  SetText(Control(window, kInputControl), message);
  Click(window, kSendControl);
}

std::size_t Occurrences(std::wstring const& log, std::wstring const& message) {
  std::size_t count = 0, position = 0;
  while ((position = log.find(message, position)) != std::wstring::npos) {
    ++count;
    position += message.size();
  }
  return count;
}

bool ContainsMessages(Process& process,
                      std::vector<std::wstring> const& messages) {
  auto const window = process.Window();
  if (!window) {
    return false;
  }
  auto const log = Text(Control(window, kMessagesControl));
  for (auto const& message : messages) {
    auto const count = Occurrences(log, message);
    Require(count <= 1, "A shared message appeared more than once");
    if (count != 1) {
      return false;
    }
  }
  return true;
}

bool ParticipantNames(Process& process) {
  auto rows = People(process.Window());
  for (auto& row : rows) {
    auto const status = row.rfind(L" [");
    if (status != std::wstring::npos) {
      row.resize(status);
    }
  }
  std::sort(rows.begin(), rows.end());
  return rows == std::vector<std::wstring>{L"Alice", L"Bob", L"Host"};
}

std::wstring Argument(int argc, wchar_t** argv, wchar_t const* option) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == std::wstring{option}) {
      return argv[index + 1];
    }
  }
  return {};
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::array<Process, 3> peers;
  running_processes = peers.data();
  process_count = peers.size();
  auto const exe_argument = Argument(argc, argv, L"--exe");
  Require(!exe_argument.empty(),
          "Usage: shared_chat_win32_test --exe <shared_node_chat.exe> "
          "[--profiles <directory>]");
  std::error_code error;
  auto const exe =
      std::filesystem::absolute(exe_argument, error).lexically_normal();
  Require(!error && std::filesystem::is_regular_file(exe, error),
          "Chat executable does not exist");
  auto const base_argument = Argument(argc, argv, L"--profiles");
  auto const base = base_argument.empty()
                        ? std::filesystem::path{L".artifacts/shared-chat-win32"}
                        : std::filesystem::path{base_argument};
  auto const absolute_base = std::filesystem::absolute(base, error);
  Require(!error, "Could not resolve test profile directory");
  profiles = absolute_base / (L"run-" + std::to_wstring(GetCurrentProcessId()) +
                              L"-" + std::to_wstring(GetTickCount64()));
  std::array<std::wstring, 3> const names{L"Host", L"Alice", L"Bob"};
  for (std::size_t index = 0; index < peers.size(); ++index) {
    peers[index].name = names[index];
    peers[index].profile = profiles / names[index];
  }
  peers[0].Start(exe);
  std::wstring invite;
  Wait("Automatically creating a host room", [&] {
    auto const window = peers[0].Window();
    if (!window || !GetDlgItem(window, kInputControl) ||
        !IsWindowEnabled(Control(window, kInputControl))) return false;
    invite = Text(Control(window, kUidControl));
    Require(IsWindowVisible(Control(window, kUidControl)) &&
            !IsWindowVisible(Control(window, kInviteControl)),
            "Host must show only its Aether ID field");
    return invite.size() == 36 && invite.find(L'/') == std::wstring::npos;
  });
  Click(peers[0].Window(), kHostControl);
  Require(OpenClipboard(nullptr) != FALSE, "Could not inspect clipboard");
  auto clipboard = GetClipboardData(CF_UNICODETEXT);
  auto* copied = clipboard ? static_cast<wchar_t*>(GlobalLock(clipboard)) : nullptr;
  bool const copied_id = copied && std::wstring{copied} == invite;
  if (copied) GlobalUnlock(clipboard);
  CloseClipboard();
  Require(copied_id, "Copy must put only the host Aether ID on the clipboard");
  peers[1].host_id = invite;
  for (std::size_t index = 1; index < peers.size(); ++index) {
    peers[index].Start(exe);
  }
  Wait("Waiting for the Aether client API", [&] {
    return Ready(peers[1]) && Ready(peers[2]);
  });
  for (std::size_t index = 1; index < peers.size(); ++index) {
    auto const window = peers[index].Window();
    Require(!IsWindowVisible(Control(window, kUidControl)) &&
            IsWindowVisible(Control(window, kInviteControl)),
            "Client must show only the host Aether ID field");
    if (index == 1) continue;  // --client UID joins without clicking.
    SetText(Control(window, kInviteControl), invite);
    Click(window, kJoinControl);
  }
  Wait("Joining three participants", [&] {
    for (auto& peer : peers) {
      auto const window = peer.Window();
      if (!ParticipantNames(peer) ||
          !IsWindowEnabled(Control(window, kInputControl))) {
        return false;
      }
    }
    return true;
  });
  auto const prefix = L"gui-" + std::to_wstring(GetCurrentProcessId());
  std::vector<std::wstring> messages{
      prefix + L"-host",
      prefix + L"-alice-\u041f\u0440\u0438\u0432\u0435\u0442",
      prefix + L"-bob-\u4f60\u597d"};
  for (std::size_t index = 0; index < peers.size(); ++index) {
    Send(peers[index], messages[index]);
  }
  Wait("Delivering messages from all three windows", [&] {
    for (auto& peer : peers) {
      if (!ContainsMessages(peer, messages)) {
        return false;
      }
    }
    return true;
  });
  auto const bob_uid = Text(Control(peers[2].Window(), kUidControl));
  peers[2].Crash();
  messages.push_back(prefix + L"-while-bob-offline");
  Send(peers[1], messages.back());
  Wait("Keeping the room usable while a participant is offline", [&] {
    return ContainsMessages(peers[0], messages) &&
           ContainsMessages(peers[1], messages);
  });
  peers[2].Start(exe);
  Wait("Restoring SDK identity and the existing chat", [&] {
    auto const window = peers[2].Window();
    return window && IsWindowEnabled(Control(window, kInputControl)) &&
           Text(Control(window, kUidControl)) == bob_uid;
  });
  Wait("Synchronizing the message sent while Bob was offline", [&] {
    return ParticipantNames(peers[2]) && ContainsMessages(peers[2], messages) &&
           IsWindowEnabled(Control(peers[2].Window(), kInputControl));
  });
  messages.push_back(prefix + L"-after-bob-restart");
  Send(peers[2], messages.back());
  Wait("Delivering from the restored participant", [&] {
    for (auto& peer : peers) {
      if (!ContainsMessages(peer, messages)) {
        return false;
      }
    }
    return true;
  });
  for (auto& peer : peers) {
    peer.Stop(true);
  }
  for (auto const& peer : peers) {
    Require(std::filesystem::exists(peer.profile / L"user.bin"),
            "Chat did not persist its native USER graph");
    Require(std::filesystem::exists(peer.profile / L"transport" / L"user.bin"),
            "Chat transport did not persist its replication USER");
    Require(!std::filesystem::exists(peer.profile / L"root.id"),
            "Chat wrote an application-root sidecar");
  }
  std::cout << "PASS: CLI host/client, UID-only discovery, Copy, single ID field, "
               "event-driven native chat, Unicode, three peers, native "
               "USER restore, offline delivery, clean shutdown"
            << std::endl;
  return 0;
}
