#include <cassert>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "chat_launch_ipc.h"
#include "chat_launch_options.h"
#include "profile_lock.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace apptraverse::example::chat_demo {
namespace {

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

void TestParserRejectsOptionAsValue() {
  {
    std::vector<std::string> args = {"--state-dir", "--peer-admin-id", "abc"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
  {
    std::vector<std::string> args = {"--peer-admin-id", "--peer-name", "n"};
    auto res = ParseChatLaunchOptions(args);
    CHECK(!res.ok);
  }
}

void TestProfileKeyNormalization() {
  std::filesystem::path const rel = "relative/profile";
  std::string key = NormalizeProfileKey(rel);
  CHECK(!key.empty());
  CHECK(key.find("relative") != std::string::npos);
}

void TestIpcCodecRoundtrip() {
  ChatLaunchIpcPayload payload{
      .profile_key = "C:/Users/test/AppTraverseChat",
      .open_peer =
          OpenPeerRequest{
              .peer_admin_id = "admin-42",
              .peer_aether_uid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
              .peer_name = "Support Agent 🚀",
          },
  };
  std::vector<std::uint8_t> bytes;
  CHECK(EncodeLaunchIpcPayload(payload, bytes));
  CHECK(!bytes.empty());

  ChatLaunchIpcPayload decoded;
  CHECK(DecodeLaunchIpcPayload(bytes, decoded));
  CHECK(decoded.profile_key == payload.profile_key);
  CHECK(decoded.open_peer == payload.open_peer);
  CHECK(ValidateLaunchIpcPayload(decoded, payload.profile_key) ==
        LaunchIpcReply::kAccepted);
}

void TestIpcRejectsMismatchAndOversized() {
  ChatLaunchIpcPayload payload{
      .profile_key = "profile-a",
      .open_peer = OpenPeerRequest{.peer_admin_id = "admin"},
  };
  CHECK(ValidateLaunchIpcPayload(payload, "profile-b") ==
        LaunchIpcReply::kProfileMismatch);

  std::vector<std::uint8_t> huge(kLaunchIpcMaxPayloadBytes + 1, 0xAB);
  ChatLaunchIpcPayload ignored;
  CHECK(!DecodeLaunchIpcPayload(huge, ignored));
}

void TestProfileLockExclusive() {
  std::filesystem::path const dir =
      std::filesystem::temp_directory_path() / "apptraverse_launch_forward_test";
  std::filesystem::remove_all(dir);

  ProfileLock first;
  CHECK(ProfileLock::TryAcquire(dir, first) == ProfileLock::AcquireResult::kAcquired);
  CHECK(first.owns_lock());

  ProfileLock second;
  CHECK(ProfileLock::TryAcquire(dir, second) == ProfileLock::AcquireResult::kBusy);
  CHECK(!second.owns_lock());

  first = ProfileLock{};
  CHECK(ProfileLock::TryAcquire(dir, second) == ProfileLock::AcquireResult::kAcquired);
  second = ProfileLock{};
  std::filesystem::remove_all(dir);
}

#ifdef _WIN32
struct IpcTestState {
  std::string profile_key;
  bool accepted{false};
  OpenPeerRequest received;
};

LRESULT CALLBACK TestIpcWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return TRUE;
  }
  auto* state = reinterpret_cast<IpcTestState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (state != nullptr && msg == WM_COPYDATA) {
    auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lparam);
    if (cds == nullptr || cds->dwData != kLaunchIpcCopyDataMagic) {
      return static_cast<LRESULT>(LaunchIpcReply::kInvalidPayload);
    }
    std::vector<std::uint8_t> bytes(cds->cbData);
    std::memcpy(bytes.data(), cds->lpData, cds->cbData);
    ChatLaunchIpcPayload payload;
    if (!DecodeLaunchIpcPayload(bytes, payload)) {
      return static_cast<LRESULT>(LaunchIpcReply::kInvalidPayload);
    }
    if (ValidateLaunchIpcPayload(payload, state->profile_key) != LaunchIpcReply::kAccepted) {
      return static_cast<LRESULT>(LaunchIpcReply::kProfileMismatch);
    }
    state->accepted = true;
    state->received = payload.open_peer;
    return static_cast<LRESULT>(LaunchIpcReply::kAccepted);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void TestForwardLaunchToPrimaryWindow() {
  HINSTANCE const hinst = GetModuleHandleW(nullptr);
  IpcTestState state{.profile_key = "C:/Temp/ProfileA"};
  std::wstring const ipc_class = ProfileRoutingWindowClass(state.profile_key);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &TestIpcWndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = ipc_class.c_str();
  RegisterClassW(&wc);

  std::wstring const title = ProfileRoutingWindowTitle(state.profile_key);
  HWND const hwnd =
      CreateWindowExW(WS_EX_TOOLWINDOW, ipc_class.c_str(), title.c_str(), WS_POPUP, 0, 0, 0, 0,
                      nullptr, nullptr, hinst, &state);
  CHECK(hwnd != nullptr);
  ShowWindow(hwnd, SW_HIDE);

  OpenPeerRequest const req{
      .peer_admin_id = "forward-admin",
      .peer_aether_uid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee",
      .peer_name = "Forwarded Name",
  };
  ForwardLaunchResult const result = TryForwardLaunchToPrimary(state.profile_key, req);
  CHECK(result == ForwardLaunchResult::kAccepted);
  CHECK(state.accepted);
  CHECK(state.received.peer_admin_id == req.peer_admin_id);
  CHECK(state.received.peer_name == req.peer_name);
  CHECK(state.received.peer_aether_uid == req.peer_aether_uid);

  DestroyWindow(hwnd);
  UnregisterClassW(ipc_class.c_str(), hinst);
}

void TestDifferentProfilesDoNotCrossForward() {
  IpcTestState state{.profile_key = "C:/Temp/ProfileA"};
  HINSTANCE const hinst = GetModuleHandleW(nullptr);
  std::wstring const ipc_class = ProfileRoutingWindowClass(state.profile_key);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &TestIpcWndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = ipc_class.c_str();
  RegisterClassW(&wc);

  std::wstring const title = ProfileRoutingWindowTitle(state.profile_key);
  HWND const hwnd =
      CreateWindowExW(WS_EX_TOOLWINDOW, ipc_class.c_str(), title.c_str(), WS_POPUP, 0, 0, 0, 0,
                      nullptr, nullptr, hinst, &state);
  CHECK(hwnd != nullptr);
  ShowWindow(hwnd, SW_HIDE);

  OpenPeerRequest const req{.peer_admin_id = "other-profile"};
  ForwardLaunchResult const result =
      TryForwardLaunchToPrimary("C:/Temp/ProfileB", req, std::chrono::milliseconds{500}, 2);
  CHECK(result == ForwardLaunchResult::kNoPrimary);
  CHECK(!state.accepted);

  DestroyWindow(hwnd);
  UnregisterClassW(ipc_class.c_str(), hinst);
}
#endif

}  // namespace
}  // namespace apptraverse::example::chat_demo

int main() {
  using namespace apptraverse::example::chat_demo;
  std::cout << "Running apptraverse_chat_launch_forward_test...\n";

  TestParserRejectsOptionAsValue();
  TestProfileKeyNormalization();
  TestIpcCodecRoundtrip();
  TestIpcRejectsMismatchAndOversized();
  TestProfileLockExclusive();
#ifdef _WIN32
  TestForwardLaunchToPrimaryWindow();
  TestDifferentProfilesDoNotCrossForward();
#endif

  std::cout << "All chat launch forward tests passed!\n";
  return 0;
}
