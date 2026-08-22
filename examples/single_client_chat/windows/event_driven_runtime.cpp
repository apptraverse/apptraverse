#include "event_driven_runtime.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include "aether/all.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/domain_snapshot_io.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "model/application_ids.h"
#include "model/app.h"
#include "model/chat.h"
#include "model/chat_peer_set.h"
#include "model/client.h"
#include "model/registration.h"

#include "../common/aether_p2p_transport.h"
#include "../common/aether_runtime.h"
#include "../common/chat_component.h"
#include "../common/chat_presentation.h"
#include "../common/graph_builder.h"
#include "latency_trace.h"
#include "room_trace.h"
#include "win_chat_transcript.h"
#include "win_add_peer_dialog.h"
#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "windows_window.h"

#include "model/chat_events.h"
#include "model/chat_room_local_state.h"
#include "model/client.h"
#include "room_control.h"
#include "room_inbound_demux.h"
#include "room_membership_controller.h"
#include "chat_component_graph.h"

namespace apptraverse::examples {
namespace {

constexpr UINT kWmPresentation = WM_APP + 61;
constexpr UINT kWmSendEnabled = WM_APP + 62;
constexpr auto kBusinessIdleCap = std::chrono::milliseconds{100};
// Cap for Aether WaitUntil when the outbound network queue is idle. Queued
// sends must wake via task_scheduler->Task (see wake_network), not by shrinking
// this alone.
constexpr auto kNetworkIdleCap = std::chrono::seconds{1};

using chat::ChatComponent;
using chat::ChatPresentationSnapshot;

std::int64_t UtcMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

char const* RoomControlTypeName(chat::RoomControlType type) {
  using chat::RoomControlType;
  switch (type) {
    case RoomControlType::kClientHello:
      return "ClientHello";
    case RoomControlType::kMembershipPrepare:
      return "MembershipPrepare";
    case RoomControlType::kMembershipPrepared:
      return "MembershipPrepared";
    case RoomControlType::kMembershipSnapshot:
      return "MembershipSnapshot";
    case RoomControlType::kMembershipApplied:
      return "MembershipApplied";
    case RoomControlType::kMembershipActivate:
      return "MembershipActivate";
    case RoomControlType::kMembershipActivated:
      return "MembershipActivated";
    case RoomControlType::kMembershipReject:
      return "MembershipReject";
  }
  return "Unknown";
}

char const* RoomUiStatusName(chat::RoomUiStatus status) {
  using chat::RoomUiStatus;
  switch (status) {
    case RoomUiStatus::kDisconnected:
      return "Disconnected";
    case RoomUiStatus::kConnecting:
      return "Connecting";
    case RoomUiStatus::kWaitingForPrepare:
      return "WaitingForPrepare";
    case RoomUiStatus::kWaitingForSnapshot:
      return "WaitingForSnapshot";
    case RoomUiStatus::kWaitingForActivate:
      return "WaitingForActivate";
    case RoomUiStatus::kWaitingForOwnJoin:
      return "WaitingForOwnJoin";
    case RoomUiStatus::kActive:
      return "Active";
    case RoomUiStatus::kError:
      return "Error";
  }
  return "Unknown";
}

std::string WideToUtf8(std::wstring const& wide) {
  if (wide.empty()) {
    return {};
  }
  int const size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                       static_cast<int>(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring Utf8ToWide(std::string const& utf8) {
  if (utf8.empty()) {
    return {};
  }
  int const size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      out.data(), size);
  return out;
}

// ---------------------------------------------------------------------------
// Queues
// ---------------------------------------------------------------------------

struct SubmitTextCommand {
  std::string text;
  std::string text_key;
};

struct AddPeerCommand {
  ae::Uid uid;
  std::string uid_text;
};

struct InboundNetworkPacket {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

struct NetworkReadyEvent {};

struct TransportSessionReadyCommand {
  ae::Uid peer;
  std::string source;
  std::uint64_t generation{0};
};

struct StopBusinessCommand {};
struct BeginShutdownCommand {};
struct FinalizeShutdownCommand {};

using BusinessItem =
    std::variant<SubmitTextCommand, AddPeerCommand, InboundNetworkPacket,
                 NetworkReadyEvent, TransportSessionReadyCommand,
                 StopBusinessCommand, BeginShutdownCommand,
                 FinalizeShutdownCommand>;

struct ConnectPeerCommand {
  ae::Uid uid;
};

struct ReconnectPeerCommand {
  ae::Uid uid;
};

struct SendSyncCommand {
  ae::Uid peer;
  ae::ObjId packet_id;
  SerializedSyncPacket bytes;
  std::string text_key;
  std::optional<std::uint32_t> event_id;
};

struct SendRawCommand {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

// Room membership control only — never SyncPacketWriteGate / packet_id.
struct SendRoomControlCommand {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
  std::string type_name;
  std::uint64_t revision{0};
};

struct StopNetworkCommand {};

using NetworkItem =
    std::variant<ConnectPeerCommand, ReconnectPeerCommand, SendSyncCommand,
                 SendRawCommand, SendRoomControlCommand, StopNetworkCommand>;

template <typename T>
class WakeQueue {
 public:
  void Push(T item) {
    {
      std::scoped_lock lock{mu_};
      items_.push_back(std::move(item));
    }
    cv_.notify_one();
  }

  template <typename Pred>
  bool WaitPop(T& out, Pred should_stop,
               std::chrono::milliseconds max_wait) {
    std::unique_lock lock{mu_};
    cv_.wait_for(lock, max_wait, [&] {
      return should_stop() || !items_.empty();
    });
    if (items_.empty()) {
      return false;
    }
    out = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  bool TryPop(T& out) {
    std::scoped_lock lock{mu_};
    if (items_.empty()) {
      return false;
    }
    out = std::move(items_.front());
    items_.pop_front();
    return true;
  }

  void Notify() { cv_.notify_one(); }

  // Wait until an item is queued or timeout. Used so the network thread can
  // sleep on the same CV that Push/Notify wakes — Aether WaitUntil alone is
  // not interrupted by network_q.Notify().
  template <typename Pred>
  void Wait(std::chrono::milliseconds max_wait, Pred should_stop) {
    std::unique_lock lock{mu_};
    cv_.wait_for(lock, max_wait, [&] {
      return should_stop() || !items_.empty();
    });
  }

 private:
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<T> items_;
};

// ---------------------------------------------------------------------------
// Pure Win32 UI (no Domain objects)
// ---------------------------------------------------------------------------

class EventDrivenUi {
 public:
  using SubmitFn = std::function<void(std::string text)>;
  using AddPeerFn = std::function<AddPeerUiResult(std::string const&)>;
  using ConnectHostFn = std::function<void(std::string const& host_uid)>;
  using PresentationFn = std::function<void(ChatPresentationSnapshot const&)>;

  struct RoomUiMode {
    bool is_host{true};
    bool show_own_uid{true};
    bool show_host_uid_field{false};
    bool send_enabled{true};
  };

  void ConfigureRoom(RoomUiMode mode) { room_mode_ = mode; }

  void SetHandlers(SubmitFn submit, AddPeerFn add_peer,
                   std::string local_uid, LatencyTrace* trace) {
    submit_ = std::move(submit);
    add_peer_ = std::move(add_peer);
    local_uid_ = std::move(local_uid);
    trace_ = trace;
  }

  void SetRoomTrace(RoomTrace* room_trace) { room_trace_ = room_trace; }

  void SetConnectHost(ConnectHostFn fn) { connect_host_ = std::move(fn); }

  void SetSendEnabled(bool enabled) {
    room_mode_.send_enabled = enabled;
    if (send_ != nullptr) {
      EnableWindow(send_, enabled ? TRUE : FALSE);
    }
    if (edit_ != nullptr) {
      EnableWindow(edit_, enabled ? TRUE : FALSE);
    }
  }

  void SetOnApplied(PresentationFn on_applied) {
    on_applied_ = std::move(on_applied);
  }

  HWND Create(std::wstring title) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &EventDrivenUi::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                            120, 80, 720, 520, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    assert(hwnd_ != nullptr);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return hwnd_;
  }

  HWND hwnd() const { return hwnd_; }

  void PostSnapshot(ChatPresentationSnapshot snapshot) {
    if (hwnd_ == nullptr) {
      return;
    }
    auto* heap = new ChatPresentationSnapshot(std::move(snapshot));
    if (!PostMessageW(hwnd_, kWmPresentation, 0,
                      reinterpret_cast<LPARAM>(heap))) {
      delete heap;
    }
  }

  void PostSendEnabled(bool enabled) {
    if (hwnd_ == nullptr) {
      return;
    }
    PostMessageW(hwnd_, kWmSendEnabled, enabled ? 1 : 0, 0);
  }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseEventDrivenChat";

  static EventDrivenUi* FromHwnd(HWND hwnd) {
    return reinterpret_cast<EventDrivenUi*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<EventDrivenUi*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = FromHwnd(hwnd);
    if (self == nullptr) {
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return self->Handle(hwnd, msg, wparam, lparam);
  }

  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_CREATE:
        CreateControls(hwnd);
        return 0;
      case WM_SIZE:
        Layout(LOWORD(lparam), HIWORD(lparam));
        return 0;
      case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
          if (reinterpret_cast<HWND>(lparam) == send_) {
            OnSend();
            return 0;
          }
          if (reinterpret_cast<HWND>(lparam) == add_) {
            OnAdd();
            return 0;
          }
          if (reinterpret_cast<HWND>(lparam) == connect_) {
            OnConnectHost();
            return 0;
          }
        }
        return 0;
      case kWmPresentation: {
        std::unique_ptr<ChatPresentationSnapshot> snap(
            reinterpret_cast<ChatPresentationSnapshot*>(lparam));
        ApplySnapshot(*snap);
        return 0;
      }
      case kWmSendEnabled:
        SetSendEnabled(wparam != 0);
        return 0;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
  }

  void CreateControls(HWND parent) {
    int top = 0;
    if (room_mode_.show_own_uid && !local_uid_.empty()) {
      uid_ = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(local_uid_).c_str(),
          WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL, 0, 0, 0, 0,
          parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(10)),
          GetModuleHandleW(nullptr), nullptr);
      top = 36;
    }
    if (room_mode_.show_host_uid_field) {
      host_uid_ = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, parent,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(11)),
          GetModuleHandleW(nullptr), nullptr);
      connect_ = CreateWindowExW(
          0, L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
          0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(12)),
          GetModuleHandleW(nullptr), nullptr);
      top = 36;
    }
    transcript_top_ = top;
    transcript_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
        GetModuleHandleW(nullptr), nullptr);
    edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)),
        GetModuleHandleW(nullptr), nullptr);
    send_ = CreateWindowExW(
        0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
        0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)),
        GetModuleHandleW(nullptr), nullptr);
    // Host/client room mode: never show free-form Add Peer "+".
    add_ = nullptr;
    SetSendEnabled(room_mode_.send_enabled);
  }

  void Layout(int width, int height) {
    int const margin = 8;
    int const edit_h = 28;
    int const send_w = 80;
    int y = margin;
    if (uid_ != nullptr) {
      MoveWindow(uid_, margin, y, width - 2 * margin, edit_h, TRUE);
      y += edit_h + margin;
    }
    if (host_uid_ != nullptr) {
      int const connect_w = 90;
      MoveWindow(host_uid_, margin, y, width - 3 * margin - connect_w, edit_h,
                 TRUE);
      if (connect_ != nullptr) {
        MoveWindow(connect_, width - margin - connect_w, y, connect_w, edit_h,
                   TRUE);
      }
      y += edit_h + margin;
    }
    int const bottom = height - margin - edit_h;
    int const transcript_h = bottom - y;
    if (transcript_ != nullptr) {
      MoveWindow(transcript_, margin, y, width - 2 * margin,
                 transcript_h > 0 ? transcript_h : 0, TRUE);
    }
    int const edit_w = width - 3 * margin - send_w;
    if (edit_ != nullptr) {
      MoveWindow(edit_, margin, bottom, edit_w > 0 ? edit_w : 0, edit_h, TRUE);
    }
    if (send_ != nullptr) {
      MoveWindow(send_, width - margin - send_w, bottom, send_w, edit_h, TRUE);
    }
  }

  void OnSend() {
    if (!room_mode_.send_enabled) {
      return;
    }
    if (edit_ == nullptr || !submit_) {
      return;
    }
    int const len = GetWindowTextLengthW(edit_);
    std::wstring wide;
    wide.resize(static_cast<std::size_t>(len) + 1);
    int const copied = GetWindowTextW(edit_, &wide[0], len + 1);
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    while (!wide.empty() &&
           (wide.back() == L'\r' || wide.back() == L'\n' ||
            wide.back() == L' ')) {
      wide.pop_back();
    }
    if (wide.empty()) {
      return;
    }
    auto text = WideToUtf8(wide);
    if (trace_ != nullptr) {
      trace_->Mark(TraceThreadRole::kUi, LatencyTrace::Marker::kUiSendClick,
                   text.c_str());
    }
    submit_(std::move(text));
    SetWindowTextW(edit_, L"");
  }

  void OnAdd() {
    // Room mode disables free-form Add Peer.
  }

  void OnConnectHost() {
    if (host_uid_ == nullptr || !connect_host_) {
      return;
    }
    int const len = GetWindowTextLengthW(host_uid_);
    std::wstring wide;
    wide.resize(static_cast<std::size_t>(len) + 1);
    int const copied = GetWindowTextW(host_uid_, &wide[0], len + 1);
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    auto text = WideToUtf8(wide);
    if (text.empty()) {
      return;
    }
    if (room_trace_ != nullptr && room_trace_->enabled()) {
      room_trace_->Event("CLIENT_CONNECT_CLICK", text, "out", "Connect", {}, {},
                         {}, "ok");
    }
    EnableWindow(host_uid_, FALSE);
    EnableWindow(connect_, FALSE);
    connect_host_(text);
  }

  void ApplySnapshot(ChatPresentationSnapshot const& snapshot) {
    if (transcript_ == nullptr) {
      return;
    }
    delivery_cache_.SeedPersistedFromSnapshot(snapshot);
    auto const apply_us = static_cast<std::uint64_t>(UtcMicros());
    auto const utf8 = FormatWindowsChatPresentationUtf8(
        snapshot, &delivery_cache_, apply_us);
    if (utf8 == last_transcript_utf8_) {
      return;
    }
    std::wstring text = Utf8ToWide(utf8);
    std::wstring crlf;
    crlf.reserve(text.size() + 8);
    for (wchar_t ch : text) {
      if (ch == L'\n') {
        crlf += L"\r\n";
      } else if (ch != L'\r') {
        crlf.push_back(ch);
      }
    }
    SetWindowTextW(transcript_, crlf.c_str());
    SendMessageW(transcript_, EM_SETSEL, static_cast<WPARAM>(crlf.size()),
                 static_cast<LPARAM>(crlf.size()));
    SendMessageW(transcript_, EM_SCROLLCARET, 0, 0);
    last_transcript_utf8_ = utf8;
    if (room_trace_ != nullptr && room_trace_->enabled()) {
      if (room_mode_.is_host) {
        room_trace_->Event("HOST_TRANSCRIPT_APPLIED", {}, {}, {}, {}, {},
                           std::to_string(utf8.size()), "ok");
      } else {
        room_trace_->Event("CLIENT_TRANSCRIPT_APPLIED", {}, {}, {}, {}, {},
                           std::to_string(utf8.size()), "ok");
      }
      room_trace_->Event("UI_TRANSCRIPT_APPLIED", {}, {}, {}, {}, {},
                         std::to_string(utf8.size()), "ok");
    }
    if (trace_ != nullptr) {
      // After the control text has been updated.
      std::string last_key;
      auto const sep = utf8.rfind(": ");
      if (sep != std::string::npos) {
        auto line_end = utf8.find('\n', sep);
        last_key = utf8.substr(
            sep + 2,
            line_end == std::string::npos ? std::string::npos : line_end - sep - 2);
        while (!last_key.empty() &&
               (last_key.back() == '\r' || last_key.back() == '\n')) {
          last_key.pop_back();
        }
        auto const latency_pos = last_key.rfind(" [");
        if (latency_pos != std::string::npos &&
            last_key.size() >= latency_pos + 5 &&
            last_key.compare(last_key.size() - 4, 4, " ms]") == 0) {
          last_key.resize(latency_pos);
        }
      }
      std::optional<std::uint32_t> event_id;
      for (auto it = snapshot.timeline.rbegin();
           it != snapshot.timeline.rend(); ++it) {
        if (it->kind == chat::ChatTimelineItemKind::kMessage) {
          event_id = it->event_obj_id;
          if (last_key.empty()) {
            last_key = it->text;
          }
          break;
        }
      }
      trace_->Mark(TraceThreadRole::kUi,
                   LatencyTrace::Marker::kUiTranscriptApplied,
                   last_key.c_str(), event_id);
    }
    if (on_applied_) {
      on_applied_(snapshot);
    }
  }

  HWND hwnd_{nullptr};
  HWND transcript_{nullptr};
  HWND edit_{nullptr};
  HWND send_{nullptr};
  HWND add_{nullptr};
  HWND uid_{nullptr};
  HWND host_uid_{nullptr};
  HWND connect_{nullptr};
  int transcript_top_{0};
  RoomUiMode room_mode_{};
  std::string local_uid_;
  std::string last_transcript_utf8_;
  WindowsTranscriptDeliveryCache delivery_cache_;
  SubmitFn submit_;
  AddPeerFn add_peer_;
  ConnectHostFn connect_host_;
  PresentationFn on_applied_;
  LatencyTrace* trace_{nullptr};
  RoomTrace* room_trace_{nullptr};
};

// ---------------------------------------------------------------------------
// Distill / Run
// ---------------------------------------------------------------------------

std::filesystem::path AetherRoot(std::filesystem::path const& state_dir) {
  return state_dir / "aether";
}

std::filesystem::path ModelRoot(std::filesystem::path const& state_dir) {
  return state_dir / "model";
}

}  // namespace

int DistillEventDriven(EventDrivenCliOptions const& options) {
  if (!options.role.has_value() || options.participant_name.empty() ||
      options.title.empty()) {
    std::cerr << "distill requires --role, --title, and --name\n";
    return 1;
  }
  if (options.role == ChatRoomRole::kHost && options.host_uid.has_value()) {
    std::cerr << "--host-uid is only valid with --role client\n";
    return 1;
  }

  std::filesystem::remove_all(options.state_dir);
  auto const model_root = ModelRoot(options.state_dir);
  auto const aether_root = AetherRoot(options.state_dir);
  std::filesystem::create_directories(model_root);
  std::filesystem::create_directories(aether_root);

  ae::RamDomainStorage model_ram;
  ae::Domain model_domain{ae::Now(), model_ram};
  auto const join_policy = *options.role == ChatRoomRole::kHost
                               ? chat::LocalJoinPolicy::kJoinLocal
                               : chat::LocalJoinPolicy::kDoNotJoinLocal;
  auto graph = BuildSingleClientChatGraph<WindowsWindow, WinWindowPresenter,
                                          WinChatPresenter>(
      model_domain, options.participant_name, join_policy);

  auto room = ChatRoomLocalState::ptr::Create(
      ae::CreateWith{model_domain}.with_id(
          ToObjId(ApplicationObjId::ChatRoomLocalState)));
  room->role = *options.role;
  room->local_client_obj_id = graph.local_client.id().id();
  room->local_display_name = options.participant_name;
  if (options.host_uid.has_value()) {
    room->host_uid = FormatAetherUid(*options.host_uid);
  }
  if (*options.role == ChatRoomRole::kHost) {
    room->active_membership_revision = 0;
    // HostBootstrap fills revision=1 + self UID at first Run.
  } else {
    room->active_membership_revision = 0;
  }
  room.Save();

  graph.app.Save();
  graph.chat.Save();
  graph.peer_set.Save();
  graph.local_client.Save();
  if (graph.window.is_valid()) {
    graph.window.Save();
  }
  SaveDirectorySnapshot(model_ram, model_root);

  {
    auto runtime = ConstructAetherAppWithEthernet([aether_root]() {
      return std::make_unique<DirectoryDomainStorage>(aether_root);
    });
    if (runtime.app) {
      (void)runtime.app->Update(ae::Now());
    }
  }

  std::cout << "Distilled event-driven chat graph to "
            << options.state_dir.string() << " (model snapshot+aether)\n";
  std::cout << "APP_CLIENT_READY platform=windows obj_id="
            << graph.local_client.id().id()
            << " name=" << graph.local_client->name << " role="
            << (*options.role == ChatRoomRole::kHost ? "host" : "client")
            << '\n';
  return 0;
}

int RunEventDriven(EventDrivenCliOptions const& options) {
  LatencyTrace trace;
  if (options.latency_trace.has_value()) {
    trace.Open(*options.latency_trace);
  } else if (char const* env = std::getenv("APPTRAVERSE_LATENCY_TRACE")) {
    if (env[0] != '\0') {
      trace.Open(env);
    }
  }
  SetDomainSnapshotMarkerSink([&trace](std::string const& marker) {
    auto eq = marker.find('=');
    auto const name = eq == std::string::npos ? marker : marker.substr(0, eq);
    trace.MarkSnapshot(name.c_str());
    // Never print snapshot markers on the latency path — console flush skews
    // measurements. When latency tracing is off, keep a single line for ops.
    if (!trace.enabled()) {
      std::cout << marker << " t_us=" << UtcMicros() << '\n';
    }
  });
  ResetDomainSnapshotIoStats();

  auto const aether_root = AetherRoot(options.state_dir);
  auto const model_root = ModelRoot(options.state_dir);
  if (!std::filesystem::exists(aether_root)) {
    std::cerr << "event-driven runtime requires <state>/aether "
                 "(fresh distill with --event-driven-runtime)\n";
    return 1;
  }

  auto aether_runtime = ConstructAetherAppWithEthernet([aether_root]() {
    return std::make_unique<DirectoryDomainStorage>(aether_root);
  });
  auto aether_app = std::move(aether_runtime.app);
  if (!aether_app) {
    std::cerr << "Failed to construct AetherApp\n";
    return 1;
  }

  auto aether_client =
      SelectPersistentAetherClient(*aether_app, options.aether_client_name);
  if (!aether_client) {
    std::cerr << "Failed to select Aether client\n";
    return 1;
  }
  auto const local_uid = FormatAetherUid(aether_client->uid());
  std::cout << "AETHER_CLIENT_READY platform=windows uid=" << local_uid << '\n';
  std::fflush(stdout);
  if (options.print_aether_uid) {
    std::cout << "AETHER_UID=" << local_uid << '\n';
    std::fflush(stdout);
  }

  RoomTrace room_trace;
  if (options.room_trace.has_value()) {
    room_trace.Open(*options.room_trace,
                    *options.role == ChatRoomRole::kHost ? "host" : "client",
                    aether_client->uid());
  }

  // Model is fully RAM-resident after one-shot directory import.
  auto model_storage = std::make_unique<ae::RamDomainStorage>();
  LoadDirectorySnapshot(model_root, *model_storage);
  auto model_domain =
      std::make_unique<ae::Domain>(ae::Now(), *model_storage);

  auto app = App::ptr::Declare(ae::CreateWith{*model_domain}.with_id(
      ToObjId(ApplicationObjId::Application)));
  app.Load();
  if (!app.is_loaded() || !app->window.is_valid()) {
    auto graph = BuildSingleClientChatGraph<WindowsWindow, WinWindowPresenter,
                                            WinChatPresenter>(*model_domain,
                                                              "Windows");
    app = graph.app;
    app.Save();
  }

  auto local_client = app->local_client;
  local_client.Load();
  if (!local_client.is_loaded()) {
    std::cerr << "Failed to load App.local_client\n";
    return 1;
  }

  // Resolve Chat without touching WinChatPresenter on the UI thread.
  auto window = app->window;
  window.Load();
  auto presenter = window->presenter;
  presenter.Load();
  auto& win_presenter = static_cast<WinWindowPresenter&>(*presenter);
  win_presenter.chat_presenter.Load();
  auto& chat_presenter_obj =
      static_cast<WinChatPresenter&>(*win_presenter.chat_presenter);
  chat_presenter_obj.chat.Load();
  auto chat = chat_presenter_obj.chat;

  if (!options.role.has_value() || options.participant_name.empty() ||
      options.title.empty()) {
    std::cerr << "event-driven runtime requires --role, --title, and --name\n";
    return 1;
  }

  auto room_state = LoadChatRoomLocalState(*model_domain);
  if (!room_state.is_loaded()) {
    std::cerr << "room_state_migration_required\n";
    return 1;
  }
  if (room_state->role != *options.role) {
    std::cerr << "persisted room role mismatch CLI\n";
    return 1;
  }
  if (room_state->local_display_name != options.participant_name) {
    std::cerr << "persisted participant name mismatch CLI (no silent rename)\n";
    return 1;
  }
  if (options.host_uid.has_value()) {
    if (*options.role == ChatRoomRole::kHost) {
      std::cerr << "--host-uid is only valid with --role client\n";
      return 1;
    }
    auto const cli_host = FormatAetherUid(*options.host_uid);
    if (!room_state->host_uid.empty() && room_state->host_uid != cli_host) {
      std::cerr << "persisted host UID mismatch CLI --host-uid\n";
      return 1;
    }
    if (room_state->host_uid.empty()) {
      room_state->host_uid = cli_host;
      room_state.Save();
    }
  }

  WakeQueue<BusinessItem> business_q;
  WakeQueue<NetworkItem> network_q;
  std::atomic<bool> stop{false};
  std::atomic<bool> network_stop{false};
  std::atomic<bool> ui_accepting{true};
  std::atomic<bool> network_ready{false};
  std::atomic<bool> component_stopped{false};
  std::atomic<bool> finalize_done{false};
  std::atomic<bool> shutdown_started{false};
  std::atomic<ae::TaskScheduler*> scheduler{nullptr};
  std::mutex phase_mu;
  std::condition_variable phase_cv;

  auto wake_network = [&]() {
    auto* sch = scheduler.load(std::memory_order::acquire);
    if (sch != nullptr) {
      sch->Task([]() {});
    }
    network_q.Notify();
  };

  auto wait_flag = [&](std::atomic<bool> const& flag) {
    std::unique_lock lock{phase_mu};
    phase_cv.wait(lock, [&] { return flag.load(std::memory_order::acquire); });
  };

  auto finalize_model_to_ram = [&](ChatComponent& /*component*/, App::ptr app_ptr,
                                   Chat::ptr chat_ptr) {
    chat_ptr.Load();
    if (chat_ptr.is_loaded()) {
      chat_ptr.Save();
      auto peer_set = chat_ptr->peer_set;
      peer_set.Load();
      if (peer_set.is_loaded()) {
        peer_set.Save();
        for (auto& peer : peer_set->peers) {
          if (!peer.session_state.is_valid()) {
            continue;
          }
          peer.session_state.Load();
          if (peer.session_state.is_loaded()) {
            peer.session_state.Save();
          }
        }
      }
    }
    if (app_ptr.is_valid()) {
      app_ptr.Load();
      if (app_ptr.is_loaded()) {
        if (app_ptr->window.is_valid()) {
          app_ptr->window.Load();
          if (app_ptr->window.is_loaded()) {
            app_ptr->window.Save();
          }
        }
        if (app_ptr->local_client.is_valid()) {
          app_ptr->local_client.Load();
          if (app_ptr->local_client.is_loaded()) {
            app_ptr->local_client.Save();
          }
        }
        app_ptr.Save();
      }
    }
  };

  EventDrivenUi ui;
  std::set<std::string> visible_keys;
  std::mutex visible_mu;

  auto publish_presentation = [&](ChatComponent& component) {
    auto snap = component.CapturePresentation();
    ui.PostSnapshot(std::move(snap));
  };

  struct PendingWriteCtx {
    std::string text_key;
    std::optional<std::uint32_t> event_id;
    std::optional<std::uint32_t> packet_id;
  };
  PendingWriteCtx pending_write{};

  // UI must exist before business publishes the first presentation (Join).
  // PostSnapshot silently drops when hwnd_ is null.
  ui.SetHandlers(
      [&](std::string text) {
        if (!ui_accepting.load(std::memory_order::acquire)) {
          return;
        }
        auto key = text;
        business_q.Push(SubmitTextCommand{std::move(text), std::move(key)});
      },
      [&](std::string const& remote_text) -> AddPeerUiResult {
        if (!ui_accepting.load(std::memory_order::acquire)) {
          return AddPeerUiResult::Invalid;
        }
        auto trimmed = remote_text;
        while (!trimmed.empty() &&
               (trimmed.front() == ' ' || trimmed.front() == '\t')) {
          trimmed.erase(trimmed.begin());
        }
        auto const uid = ae::Uid::FromString(std::string_view{trimmed});
        if (uid.empty()) {
          return AddPeerUiResult::Invalid;
        }
        if (uid == aether_client->uid()) {
          return AddPeerUiResult::Self;
        }
        business_q.Push(AddPeerCommand{uid, FormatAetherUid(uid)});
        return AddPeerUiResult::Ok;
      },
      local_uid, &trace);
  ui.SetRoomTrace(&room_trace);

  EventDrivenUi::RoomUiMode room_ui{};
  room_ui.is_host = (*options.role == ChatRoomRole::kHost);
  room_ui.show_own_uid = room_ui.is_host;
  room_ui.show_host_uid_field =
      !room_ui.is_host && !options.host_uid.has_value() &&
      room_state->host_uid.empty();
  room_ui.send_enabled = room_ui.is_host;
  ui.ConfigureRoom(room_ui);
  ui.SetConnectHost([&](std::string const& host_text) {
    if (!ui_accepting.load(std::memory_order::acquire)) {
      return;
    }
    auto const uid = ae::Uid::FromString(std::string_view{host_text});
    if (uid.empty()) {
      return;
    }
    business_q.Push(AddPeerCommand{uid, FormatAetherUid(uid)});
  });

  std::wstring title = Utf8ToWide(options.title);
  if (title.empty()) {
    title = L"AppTraverse Chat";
  }
  ui.Create(title);
  assert(ui.hwnd() != nullptr);

  // ---- Network thread ----
  std::thread network_thread([&]() {
    AetherP2pTransport transport;
    transport.Start(*aether_app, aether_client);
    scheduler.store(aether_app->aether()->task_scheduler.get(),
                    std::memory_order::release);

    transport.SetPreWriteHandler(
        [&](ae::Uid const& peer, std::size_t frame_bytes) {
          if (room_trace.enabled()) {
            room_trace.Event("P2P_WRITE_CALLED", FormatAetherUid(peer), "out",
                             {}, {}, {}, std::to_string(frame_bytes), "ok");
            if (*options.role == ChatRoomRole::kClient) {
              room_trace.Event("CLIENT_P2P_WRITE_CALLED",
                               FormatAetherUid(peer), "out", {}, {}, {},
                               std::to_string(frame_bytes), "ok");
            }
          }
          trace.Mark(TraceThreadRole::kNetwork,
                     LatencyTrace::Marker::kP2pWriteCalled,
                     pending_write.text_key.empty()
                         ? nullptr
                         : pending_write.text_key.c_str(),
                     pending_write.event_id, pending_write.packet_id);
        });

    if (room_trace.enabled()) {
      transport.SetLogHandler([&](std::string line) {
            if (line.find("P2P_SESSION_CREATE") != std::string::npos ||
                line.find("P2P_SESSION_REPLACE") != std::string::npos ||
                line.find("P2P_SESSION_DESTROY") != std::string::npos ||
                line.find("P2P_SESSION_WRITABLE") != std::string::npos ||
                line.find("P2P_SESSION_LINK_WAIT") != std::string::npos ||
                line.find("P2P_RECONNECT_SUPPRESSED") != std::string::npos) {
          room_trace.Event("P2P_TRANSPORT", {}, {}, {}, {}, {}, {}, line);
          room_trace.Event("CHAT_PEER_READY", {}, {}, {}, {}, {}, {}, line);
        }
      });
    }
    transport.SetSessionReadyHandler(
        [&](ae::Uid const& peer, char const* source, std::uint64_t generation) {
          business_q.Push(TransportSessionReadyCommand{
              peer, source != nullptr ? std::string{source} : std::string{},
              generation});
        });

    transport.SetReceiveHandler(
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
          std::optional<chat::RoomControlMessage> room_msg;
          auto const room_kind =
              chat::ClassifyRoomControlInbound(payload, &room_msg);
          if (room_kind == chat::RoomInboundKind::kRoomControlOk) {
            business_q.Push(InboundNetworkPacket{peer, payload});
            return;
          }
          if (room_kind == chat::RoomInboundKind::kRoomControlDecodeFail) {
            // Never treat ATRM magic payloads as Chat sync.
            return;
          }
          if (TryHandleP2pProbePayload(transport, peer, payload, {}, {})) {
            return;
          }
          trace.Mark(TraceThreadRole::kNetwork,
                     LatencyTrace::Marker::kRemoteP2pReceived);
          business_q.Push(InboundNetworkPacket{peer, payload});
        });

    network_ready.store(true, std::memory_order::release);
    business_q.Push(NetworkReadyEvent{});

    while (!network_stop.load(std::memory_order::acquire) &&
           !aether_app->IsExited()) {
      NetworkItem item;
      while (network_q.TryPop(item)) {
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, ConnectPeerCommand>) {
                if (room_trace.enabled()) {
                  room_trace.Event("NETWORK_COMMAND_DEQUEUED",
                                   FormatAetherUid(cmd.uid), "out", "Connect",
                                   {}, {}, {}, "ok");
                }
                // Create-if-missing only. Do not Drop a healthy incoming
                // session established by the remote's Connect.
                transport.Connect(cmd.uid);
                if (room_trace.enabled()) {
                  room_trace.Event("HOST_CONNECT_REQUEST",
                                   FormatAetherUid(cmd.uid), "out", "Connect",
                                   {}, {}, {}, "ok");
                }
              } else if constexpr (std::is_same_v<T, ReconnectPeerCommand>) {
                if (room_trace.enabled()) {
                  room_trace.Event("NETWORK_COMMAND_DEQUEUED",
                                   FormatAetherUid(cmd.uid), "out", "Reconnect",
                                   {}, {}, {}, "ok");
                }
                transport.Reconnect(cmd.uid);
                if (room_trace.enabled()) {
                  room_trace.Event("HOST_CONNECT_REQUEST",
                                   FormatAetherUid(cmd.uid), "out", "Reconnect",
                                   {}, {}, {}, "ok");
                }
              } else if constexpr (std::is_same_v<T, SendSyncCommand>) {
                if (room_trace.enabled()) {
                  room_trace.Event("HOST_INITIAL_SYNC_WRITE",
                                  FormatAetherUid(cmd.peer), "out", "Sync", {},
                                  {},
                                  std::to_string(cmd.bytes.size()),
                                  cmd.packet_id.id() ? "packet" : "ok");
                }
                pending_write.text_key = cmd.text_key;
                pending_write.event_id = cmd.event_id;
                pending_write.packet_id = cmd.packet_id.id();
                transport.Send(cmd.peer, cmd.bytes);
                pending_write = {};
              } else if constexpr (std::is_same_v<T, SendRawCommand>) {
                transport.Send(cmd.peer, cmd.bytes);
              } else if constexpr (std::is_same_v<T, SendRoomControlCommand>) {
                if (room_trace.enabled()) {
                  room_trace.Event(
                      "NETWORK_COMMAND_DEQUEUED", FormatAetherUid(cmd.peer),
                      "out", cmd.type_name, std::to_string(cmd.revision), {},
                      std::to_string(cmd.bytes.size()), "ok");
                  if (cmd.type_name == "ClientHello") {
                    room_trace.Event("CLIENT_HELLO_NETWORK_DEQUEUED",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, std::to_string(cmd.bytes.size()), "ok");
                  }
                }
                // If session/stream is not ready, transport.Send returns
                // without PreWrite — trace will show DEQUEUED without WRITE.
                transport.Send(cmd.peer, cmd.bytes);
                if (room_trace.enabled()) {
                  if (cmd.type_name == "ClientHello") {
                    room_trace.Event("CLIENT_HELLO_P2P_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, std::to_string(cmd.bytes.size()), "ok");
                  } else if (cmd.type_name == "MembershipPrepare") {
                    room_trace.Event("HOST_PREPARE_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, {}, "ok");
                  } else if (cmd.type_name == "MembershipPrepared") {
                    room_trace.Event("CLIENT_PREPARED_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, {}, "ok");
                  } else if (cmd.type_name == "MembershipSnapshot") {
                    room_trace.Event("HOST_SNAPSHOT_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, {}, "ok");
                  } else if (cmd.type_name == "MembershipApplied") {
                    room_trace.Event("CLIENT_APPLIED_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, {}, "ok");
                  } else if (cmd.type_name == "MembershipActivate") {
                    room_trace.Event("HOST_ACTIVATE_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, {}, "ok");
                  } else if (cmd.type_name == "MembershipActivated") {
                    room_trace.Event("CLIENT_ACTIVATED_WRITE",
                                     FormatAetherUid(cmd.peer), "out",
                                     cmd.type_name, std::to_string(cmd.revision),
                                     {}, {}, "ok");
                  }
                }
              } else if constexpr (std::is_same_v<T, StopNetworkCommand>) {
                // Tear down PeerSession subscriptions before AetherApp Exit so
                // stream callbacks cannot touch destroyed sessions.
                transport.Stop();
                network_stop.store(true, std::memory_order::release);
                aether_app->Exit(0);
              }
            },
            item);
      }

      if (network_stop.load(std::memory_order::acquire) ||
          aether_app->IsExited()) {
        break;
      }

      auto const now = ae::Now();
      auto const next = aether_app->Update(now);
      if (network_stop.load(std::memory_order::acquire) ||
          aether_app->IsExited()) {
        break;
      }
      // Wake paths while blocked here:
      // 1) Aether TaskScheduler (inbound / DelayedTask) via WaitUntil
      // 2) wake_network() → Task([]{}) which sets scheduler trigger
      // network_q.Notify() alone does not interrupt WaitUntil; Push always
      // pairs with wake_network for outbound commands.
      aether_app->WaitUntil(std::min(next, ae::Now() + kNetworkIdleCap));
    }
  });

  // ---- Business thread ----
  std::thread business_thread([&]() {
    while (!network_ready.load(std::memory_order::acquire) &&
           !stop.load(std::memory_order::acquire)) {
      BusinessItem boot;
      if (business_q.WaitPop(boot, [&] { return stop.load(); },
                             std::chrono::milliseconds{50})) {
        if (std::holds_alternative<NetworkReadyEvent>(boot)) {
          break;
        }
        if (std::holds_alternative<StopBusinessCommand>(boot)) {
          return;
        }
        business_q.Push(std::move(boot));
      }
    }
    if (stop.load(std::memory_order::acquire)) {
      return;
    }

    // Business-thread-only: active only while SubmitText runs (RAII).
    struct SubmitTraceContext {
      bool active{false};
      std::uint32_t message_index{0};
      std::string text_key;
    };
    SubmitTraceContext submit_trace{};
    std::uint32_t next_message_index{1};
    // Runtime-only packet_id → key so retries keep the original correlation.
    struct PacketTraceMeta {
      std::uint32_t message_index{0};
      std::string text_key;
      std::optional<std::uint32_t> event_id;
    };
    std::unordered_map<std::uint32_t, PacketTraceMeta> packet_trace_by_id;

    chat::RoomMembershipController* room_ptr = nullptr;

    struct SubmitTraceScope {
      SubmitTraceContext& ctx;
      explicit SubmitTraceScope(SubmitTraceContext& c, std::uint32_t index,
                                std::string key)
          : ctx{c} {
        ctx.active = true;
        ctx.message_index = index;
        ctx.text_key = std::move(key);
      }
      SubmitTraceScope(SubmitTraceScope const&) = delete;
      SubmitTraceScope& operator=(SubmitTraceScope const&) = delete;
      ~SubmitTraceScope() {
        ctx.active = false;
        ctx.message_index = 0;
        ctx.text_key.clear();
      }
    };

    ChatComponent component(
        SyncReplica{*model_domain, *model_storage, chat.id()}, local_client,
        chat,
        [&](ae::Uid const& peer, ae::ObjId packet_id,
            SerializedSyncPacket const& bytes) {
          std::string text_key;
          std::optional<std::uint32_t> event_id;
          auto const pid = packet_id.id();
          if (submit_trace.active) {
            // Local SubmitText Event path only — register for retries.
            text_key = submit_trace.text_key;
            packet_trace_by_id[pid] = PacketTraceMeta{
                submit_trace.message_index, submit_trace.text_key,
                std::nullopt};
          } else {
            auto it = packet_trace_by_id.find(pid);
            if (it != packet_trace_by_id.end()) {
              // Retry of a previously correlated Event packet.
              text_key = it->second.text_key;
              event_id = it->second.event_id;
            }
            // else: ACK / node-state / presence-adjacent sync — no key.
          }
          network_q.Push(SendSyncCommand{peer, packet_id, bytes,
                                         std::move(text_key), event_id});
          wake_network();
        },
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
          network_q.Push(SendRawCommand{peer, bytes});
          wake_network();
        },
        [&](ae::Uid const& remote_uid) {
          network_q.Push(ConnectPeerCommand{remote_uid});
          wake_network();
        },
        chat::ChatSyncTiming{},
        [&](std::string const& line) {
          // Latency path: memory only. No std::cout / fflush on the hot path.
          if (trace.enabled()) {
            trace.MarkFromProductLine(TraceThreadRole::kBusiness, line);
          } else if (room_trace.enabled()) {
            // Transition / delivery-edge markers only (no per-tick, retry-poll,
            // auth or duplicate-check dump).
            static constexpr struct {
              char const* needle;
              char const* layer;
            } kSyncTraceMarkers[] = {
                {"CHAT_EVENT_COMMITTED", "CLIENT_EVENT_COMMITTED"},
                {"SYNC_TRANSPORT_WRITE", "CLIENT_SYNC_TRANSPORT_WRITE"},
                {"SYNC_WRITE_SUPPRESSED", "SYNC_WRITE_SUPPRESSED"},
                {"CHAT_SYNC_RECONNECT_BEGIN", "CLIENT_SYNC_RECONNECT_BEGIN"},
                {"SYNC_RECONNECT_FLUSH", "SYNC_RECONNECT_FLUSH"},
                {"CHAT_PENDING_FLUSH_BEGIN", "CHAT_PENDING_FLUSH_BEGIN"},
                {"CHAT_TRANSPORT_SESSION_READY", "CHAT_TRANSPORT_SESSION_READY"},
                {"CHAT_PEER_OFFLINE", "CHAT_PEER_OFFLINE"},
                {"CHAT_PEER_REJOINED", "CHAT_PEER_REJOINED"},
                {"CHAT_PEER_ONLINE", "CHAT_PEER_ONLINE"},
                {"SYNC_TRANSPORT_RECEIVE", "SYNC_TRANSPORT_RECEIVE"},
                {"SYNC_PACKET_RECEIVED", "SYNC_PACKET_RECEIVED"},
                {"SYNC_PACKET_RETRY", "SYNC_PACKET_RETRY"},
                {"SYNC_ACK_RECEIVED", "SYNC_ACK_RECEIVED"},
                {"SYNC_PENDING_REMOVED", "SYNC_PENDING_REMOVED"},
                {"SYNC_EVENT_BLOCKED", "SYNC_EVENT_BLOCKED"},
                {"SYNC_EVENT_APPLIED", "HOST_SYNC_EVENT_APPLIED"},
                {"SYNC_INITIAL_COMPLETE", "SYNC_INITIAL_COMPLETE"},
                {"CHAT_PENDING_CHANGED", "PENDING_COUNT_CHANGED"},
            };
            for (auto const& marker : kSyncTraceMarkers) {
              if (line.find(marker.needle) != std::string::npos) {
                room_trace.Event(marker.layer, {}, {}, {}, {}, {}, {}, line);
              }
            }
            if (line.find("CHAT_TRANSPORT_SESSION_READY") != std::string::npos ||
                line.find("CHAT_PEER_ONLINE") != std::string::npos ||
                line.find("CHAT_PEER_REJOINED") != std::string::npos) {
              room_trace.Event("CLIENT_SESSION_READY", {}, {}, {}, {}, {}, {},
                               line);
              room_trace.Event("CHAT_PEER_READY", {}, {}, {}, {}, {}, {}, line);
            }
          }
          if (line.find("CHAT_PEER_OFFLINE") != std::string::npos) {
            if (*options.role == ChatRoomRole::kClient && room_ptr != nullptr) {
              room_ptr->ClientNudgeReconnect();
              if (!room_ptr->host_uid().empty()) {
                network_q.Push(ReconnectPeerCommand{room_ptr->host_uid()});
                wake_network();
              }
            }
          }
          if (line.find("SYNC_PENDING_REMOVED") != std::string::npos) {
            auto pos = line.find("packet=");
            if (pos != std::string::npos) {
              pos += 7;
              try {
                auto const pid =
                    static_cast<std::uint32_t>(std::stoul(line.substr(pos)));
                packet_trace_by_id.erase(pid);
              } catch (...) {
              }
            }
          }
        });

    // Host/client membership (business-thread only).
    // Product scope: Host + one Client (membership revision 1→2). Second
    // distinct client behavior is undefined here. Current Aether P2P transport
    // has a separately tracked large single-write limitation; this milestone
    // validates only states whose initial sync fits the current transport
    // envelope.
    chat::RoomMembershipHooks room_hooks{};
    room_hooks.send_control =
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
          std::string type_name = "Unknown";
          std::uint64_t revision = 0;
          if (auto decoded = chat::TryDecodeRoomControl(bytes)) {
            type_name = RoomControlTypeName(decoded->type);
            revision = decoded->revision;
          }
          if (room_trace.enabled()) {
            room_trace.Event("CONTROL_ENCODE", FormatAetherUid(peer), "out",
                             type_name, std::to_string(revision), {},
                             std::to_string(bytes.size()),
                             bytes.empty() ? "empty" : "ok");
            room_trace.Event("CONTROL_SEND_REQUEST", FormatAetherUid(peer),
                             "out", type_name, std::to_string(revision), {},
                             std::to_string(bytes.size()), "ok");
            room_trace.Event("NETWORK_COMMAND_ENQUEUED", FormatAetherUid(peer),
                             "out", type_name, std::to_string(revision), {},
                             std::to_string(bytes.size()), "SendRoomControl");
          }
          network_q.Push(SendRoomControlCommand{peer, bytes, type_name, revision});
          wake_network();
        };
    room_hooks.connect_peer = [&](ae::Uid const& peer) {
      if (room_trace.enabled()) {
        room_trace.Event("BUSINESS_CONNECT_REQUEST", FormatAetherUid(peer),
                         "out", "Connect", {}, {}, {}, "ok");
        room_trace.Event("NETWORK_COMMAND_ENQUEUED", FormatAetherUid(peer),
                         "out", "Connect", {}, {}, {}, "ok");
      }
      network_q.Push(ConnectPeerCommand{peer});
      wake_network();
    };
    room_hooks.add_chat_peer = [&](ae::Uid const& peer) {
      if (room_trace.enabled()) {
        room_trace.Event(
            *options.role == ChatRoomRole::kHost ? "HOST_CHAT_ADD_PEER"
                                                 : "CLIENT_CHAT_ADD_PEER",
            FormatAetherUid(peer), "out", {}, {}, {}, {}, "ok");
        room_trace.Event("ADD_PEER_REQUESTED", FormatAetherUid(peer), "out",
                         {}, {}, {}, {}, "ok");
        room_trace.Event("CHAT_ADD_PEER_REQUEST", FormatAetherUid(peer), "out",
                         {}, {}, {}, {}, "ok");
        if (*options.role == ChatRoomRole::kHost) {
          room_trace.Event("HOST_INITIAL_SYNC_CREATED", FormatAetherUid(peer),
                           "out", {}, {}, {}, {}, "pending_add");
        }
      }
      auto const result = component.AddPeer(peer);
      if (room_trace.enabled()) {
        char const* r = "unknown";
        switch (result) {
          case chat::AddPeerResult::kAdded:
            r = "added";
            break;
          case chat::AddPeerResult::kAlreadyPresent:
            r = "already_present";
            break;
          case chat::AddPeerResult::kInvalidUid:
            r = "invalid_uid";
            break;
          case chat::AddPeerResult::kNotRunning:
            r = "not_running";
            break;
        }
        room_trace.Event("CHAT_ADD_PEER_RESULT", FormatAetherUid(peer), "out",
                         {}, {}, {}, {}, r);
        if (*options.role == ChatRoomRole::kHost &&
            result == chat::AddPeerResult::kAdded) {
          room_trace.Event("HOST_INITIAL_SYNC_CREATED", FormatAetherUid(peer),
                           "out", {}, {}, {}, {}, "started");
        }
      }
    };
    room_hooks.ensure_host_join =
        [&](ae::Uid const& uid, std::uint32_t client_obj_id,
            std::string const& name) -> bool {
          if (room_trace.enabled()) {
            room_trace.Event("HOST_CLIENT_JOIN_COMMIT_BEGIN",
                             FormatAetherUid(uid), "out", "JoinClientEvent", {},
                             {}, std::to_string(client_obj_id), name);
          }
          chat.Load();
          for (auto const& record : chat->journal) {
            if (!record.event.is_valid() ||
                record.event->GetClassId() != JoinClientEvent::kClassId) {
              continue;
            }
            auto join = JoinClientEvent::ptr{record.event};
            join.Load();
            if (join.is_loaded() && join->client.is_valid() &&
                join->client.id().id() == client_obj_id) {
              if (room_trace.enabled()) {
                room_trace.Event("HOST_CLIENT_JOIN_COMMIT_END",
                                 FormatAetherUid(uid), "out", "JoinClientEvent",
                                 {}, {}, {}, "already_present");
              }
              return false;
            }
          }
          auto client_base = Client::ptr::Create(ae::CreateWith{*model_domain});
          auto client = Client::ptr::Create(
              ae::CreateWith{*model_domain}.with_id(client_obj_id));
          client->name = name;
          client->base = client_base;
          client->CaptureBaseState();
          client.Save();
          auto join =
              JoinClientEvent::ptr::Create(ae::CreateWith{*model_domain});
          join->client = client;
          chat->Commit(join);
          chat.Save();
          EventRecord const* committed = nullptr;
          for (auto const& record : chat->journal) {
            if (record.event.is_valid() && record.event.id() == join.id()) {
              committed = &record;
              break;
            }
          }
          if (committed != nullptr) {
            component.PublishCommittedJournalEvent(*committed);
          }
          // Join must be in the host journal before HostFinishActivation
          // AddPeer / SYNC_INITIAL_BUILD so the late participant's initial
          // NodeState includes their Join. Do NOT AddPeer here: Client has not
          // applied Snapshot yet, so inbound sync is unauthorized and dropped;
          // the write gate then delays the real first sync by ~retry interval.
          if (room_trace.enabled()) {
            room_trace.Event("JOIN_CREATED", {}, {}, "JoinClientEvent",
                             std::to_string(join.id().id()), {},
                             std::to_string(client_obj_id), name);
            room_trace.Line(
                "layer=JOIN_IDENTITY result=host_client_obj_id=" +
                std::to_string(client_obj_id) +
                " join_event_obj_id=" + std::to_string(join.id().id()) +
                " name=" + name);
            room_trace.Event("HOST_CLIENT_JOIN_COMMIT_END", FormatAetherUid(uid),
                             "out", "JoinClientEvent",
                             std::to_string(join.id().id()), {}, {}, "ok");
            room_trace.Event("MEMBERSHIP_REVISION_COMMITTED", {}, {}, {}, {},
                             {}, {}, "ok");
          }
          return true;
        };
    room_hooks.has_local_join = [&] { return component.HasLocalJoin(); };
    room_hooks.probe_local_join = [&] {
      chat::RoomLocalJoinIdentity id{};
      auto const probe = component.ProbeLocalJoin();
      id.local_client_obj_id = probe.local_client_obj_id;
      id.join_client_obj_id = probe.join_client_obj_id;
      id.obj_id_match =
          probe.kind == chat::ChatComponent::LocalJoinMatchKind::kObjId;
      id.name_fallback =
          probe.kind == chat::ChatComponent::LocalJoinMatchKind::kNameFallback;
      return id;
    };
    bool last_send_enabled = *options.role == ChatRoomRole::kHost;
    room_hooks.on_ui_changed = [&] {
      if (room_ptr != nullptr) {
        bool const send_ok = room_ptr->CanSendChat();
        if (send_ok != last_send_enabled) {
          if (room_trace.enabled()) {
            room_trace.Event(
                "UI_SEND_ENABLED_CHANGED", {}, {}, {}, {},
                std::string(last_send_enabled ? "true" : "false") + "->" +
                    (send_ok ? "true" : "false"),
                {}, send_ok ? "1" : "0");
          }
          last_send_enabled = send_ok;
        }
        ui.PostSendEnabled(send_ok);
      }
    };
    room_hooks.on_model_changed = [&] { publish_presentation(component); };
    room_hooks.log = [&](std::string const& line) {
      // Transition markers only; never dump controller logs to stdout/stderr.
      if (room_trace.enabled()) {
        room_trace.Line("layer=ROOM_TRANSITION result=" + line);
      }
    };

    chat::RoomMembershipController room{
        *options.role, aether_client->uid(), local_client.id().id(),
        options.participant_name, room_state, room_hooks};
    room_ptr = &room;
    if (room_trace.enabled()) {
      room_trace.Line("layer=LOCAL_CLIENT_IDENTITY result=obj_id=" +
                      std::to_string(local_client.id().id()) + " name=" +
                      options.participant_name);
    }

    component.SetIncomingPeerAuthorize(
        [&](ae::Uid const& peer) { return room.IsAuthorizedSyncPeer(peer); });

    if (*options.role == ChatRoomRole::kHost) {
      if (room_trace.enabled()) {
        room_trace.Event("HOST_PROCESS_START", {}, {}, {}, {}, {}, {}, "ok");
        room_trace.Event("HOST_ROOM_STATE_LOADED", {}, {}, {},
                         std::to_string(room.applied_revision()), {}, {},
                         "ok");
      }
      room.HostBootstrap();
      if (room_trace.enabled()) {
        room_trace.Event("HOST_AETHER_READY", {}, {}, {}, {}, {}, {}, "ok");
        room_trace.Event("HOST_CHAT_COMPONENT_START", {}, {}, {}, {}, {}, {},
                         "pending");
      }
      // Transport connect can run before ChatComponent::Start; AddPeer cannot.
      for (auto const& p : room.ActiveParticipants()) {
        if (p.uid == aether_client->uid()) {
          continue;
        }
        if (room_trace.enabled()) {
          room_trace.Event("HOST_RECONNECT_EXPECTED_CLIENT",
                           FormatAetherUid(p.uid), "out", {}, {}, {}, {}, "ok");
        }
        if (room_hooks.connect_peer) {
          room_hooks.connect_peer(p.uid);
        }
      }
    } else {
      ae::Uid host{};
      if (options.host_uid.has_value()) {
        host = *options.host_uid;
      } else if (!room_state->host_uid.empty()) {
        host = ae::Uid::FromString(std::string_view{room_state->host_uid});
      }
      if (!host.empty()) {
        if (room_trace.enabled()) {
          room_trace.Event("UI_CONNECT", FormatAetherUid(host), "out",
                           "ClientHello", {}, "Disconnected->Connecting", {},
                           "auto");
        }
        room.ClientConnect(host);
      }
    }

    component.Start();
    if (*options.role == ChatRoomRole::kHost) {
      // Restore Chat mesh for persisted accepted Client after Start so AddPeer
      // is running. Complements connect_peer above (transport-only pre-Start).
      for (auto const& p : room.ActiveParticipants()) {
        if (p.uid == aether_client->uid()) {
          continue;
        }
        if (room_hooks.add_chat_peer) {
          room_hooks.add_chat_peer(p.uid);
        }
      }
    }
    publish_presentation(component);

    auto maybe_peer_inbox = [&]() {};

    while (!stop.load(std::memory_order::acquire)) {
      BusinessItem item;
      bool const got = business_q.WaitPop(
          item, [&] { return stop.load(std::memory_order::acquire); },
          kBusinessIdleCap);
      auto const now = ae::Now();
      if (got) {
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, SubmitTextCommand>) {
                if (!room.CanSendChat()) {
                  return;
                }
                if (room_trace.enabled() &&
                    *options.role == ChatRoomRole::kHost) {
                  room_trace.Event("HOST_LOCAL_SEND_CLICK", {}, "out", {}, {},
                                   {}, {}, cmd.text_key);
                }
                std::optional<std::uint32_t> event_id;
                {
                  SubmitTraceScope scope{submit_trace, next_message_index++,
                                         cmd.text_key};
                  event_id = component.SubmitText(cmd.text);
                  if (event_id.has_value()) {
                    for (auto& [pid, meta] : packet_trace_by_id) {
                      if (meta.message_index == submit_trace.message_index &&
                          meta.text_key == cmd.text_key) {
                        meta.event_id = event_id;
                      }
                    }
                  }
                }
                if (event_id.has_value()) {
                  trace.Mark(TraceThreadRole::kBusiness,
                             LatencyTrace::Marker::kEventCommitted,
                             cmd.text_key.c_str(), event_id);
                  if (!trace.enabled()) {
                    std::cout
                        << "CHAT_MESSAGE_COMMITTED platform=windows event="
                        << *event_id << " text_key=" << cmd.text_key
                        << " t_us=" << UtcMicros() << '\n';
                  }
                }
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, AddPeerCommand>) {
                // Room mode: no free-form Add Peer UI for host/client.
                if (*options.role == ChatRoomRole::kClient &&
                    room.ui_status() == chat::RoomUiStatus::kDisconnected) {
                  if (room_trace.enabled()) {
                    room_trace.Event("BUSINESS_CONNECT_DEQUEUED",
                                     FormatAetherUid(cmd.uid), "out", "Connect",
                                     {}, {}, {}, "ok");
                  }
                  room.ClientConnect(cmd.uid);
                  if (room_trace.enabled()) {
                    room_trace.Event("CLIENT_HELLO_ENQUEUED",
                                     FormatAetherUid(cmd.uid), "out",
                                     "ClientHello", {}, {}, {}, "ok");
                  }
                }
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, TransportSessionReadyCommand>) {
                if (room_trace.enabled()) {
                  if (*options.role == ChatRoomRole::kHost) {
                    room_trace.Event(
                        "HOST_SESSION_READY", FormatAetherUid(cmd.peer), "out",
                        {}, {}, {}, std::to_string(cmd.generation), cmd.source);
                  } else {
                    room_trace.Event(
                        "CLIENT_SESSION_READY", FormatAetherUid(cmd.peer), "out",
                        {}, {}, {}, std::to_string(cmd.generation), cmd.source);
                    room_trace.Event(
                        "CLIENT_HOST_RECONNECTED", FormatAetherUid(cmd.peer),
                        "out", {}, {}, {}, std::to_string(cmd.generation),
                        cmd.source);
                  }
                  room_trace.Event(
                      "HOST_CHAT_PEER_READY", FormatAetherUid(cmd.peer), "out",
                      {}, {}, {}, std::to_string(cmd.generation), cmd.source);
                }
                component.NotifyTransportSessionReady(cmd.peer, cmd.generation);
              } else if constexpr (std::is_same_v<T, InboundNetworkPacket>) {
                std::optional<chat::RoomControlMessage> room_msg;
                auto const room_kind =
                    chat::ClassifyRoomControlInbound(cmd.bytes, &room_msg);
                if (room_kind == chat::RoomInboundKind::kRoomControlOk) {
                  room.OnControl(cmd.peer, *room_msg);
                } else if (room_kind ==
                           chat::RoomInboundKind::kRoomControlDecodeFail) {
                  // Drop corrupt ATRM control; never treat as Chat sync.
                } else {
                  component.Receive(cmd.peer, cmd.bytes);
                }
                if (component.HasLocalJoin()) {
                  room.NotifyLocalJoinAppeared();
                }
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, NetworkReadyEvent>) {
                // already handled at boot
              } else if constexpr (std::is_same_v<T, BeginShutdownCommand>) {
                packet_trace_by_id.clear();
                component.Stop();
                component_stopped.store(true, std::memory_order::release);
                phase_cv.notify_all();
              } else if constexpr (std::is_same_v<T, FinalizeShutdownCommand>) {
                room_state.Save();
                finalize_model_to_ram(component, app, chat);
                finalize_done.store(true, std::memory_order::release);
                phase_cv.notify_all();
                stop.store(true, std::memory_order::release);
              } else if constexpr (std::is_same_v<T, StopBusinessCommand>) {
                stop.store(true, std::memory_order::release);
              }
            },
            item);
      }

      component.Tick(now);
      room.Tick(now);
      // Event-driven: Tick may apply auto-accepted sync without an inbound
      // packet handler. Re-evaluate own Join while WaitingForOwnJoin (silent).
      if (room.ui_status() == chat::RoomUiStatus::kWaitingForOwnJoin &&
          component.HasLocalJoin()) {
        room.NotifyLocalJoinAppeared();
        publish_presentation(component);
      }
      maybe_peer_inbox();

      // Detect newly visible message keys for harness (stdout).
      auto snap = component.CapturePresentation();
      {
        std::scoped_lock lock{visible_mu};
        for (auto const& item_view : snap.timeline) {
          if (item_view.kind != chat::ChatTimelineItemKind::kMessage) {
            continue;
          }
          if (!visible_keys.insert(item_view.text).second) {
            continue;
          }
          if (!trace.enabled()) {
            std::cout << "CHAT_MESSAGE_VISIBLE platform=windows text_key="
                      << item_view.text << " t_us=" << UtcMicros() << '\n';
          }
        }
      }
    }

    if (!component_stopped.load(std::memory_order::acquire)) {
      component.Stop();
    }
  });

  // ---- UI thread message loop (HWND already created) ----
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // Ordered shutdown: UI closed → stop component → network join → drain →
  // final RAM serialize → one directory snapshot. Idempotent on duplicate close.
  bool expected = false;
  if (!shutdown_started.compare_exchange_strong(expected, true)) {
    return static_cast<int>(msg.wParam);
  }

  ui_accepting.store(false, std::memory_order::release);
  business_q.Push(BeginShutdownCommand{});
  wait_flag(component_stopped);

  network_q.Push(StopNetworkCommand{});
  wake_network();
  // Do not call AetherApp::Exit on the UI thread — network owns Exit.
  if (network_thread.joinable()) {
    network_thread.join();
  }
  scheduler.store(nullptr, std::memory_order::release);

  business_q.Push(FinalizeShutdownCommand{});
  wait_flag(finalize_done);
  if (business_thread.joinable()) {
    business_thread.join();
  }

  SaveDirectorySnapshot(*model_storage, model_root);

  // Do not reset model_domain / aether_app while App/Chat Ptrs remain in
  // scope — let reverse-declaration destructors run after return.
  SetDomainSnapshotMarkerSink({});
  trace.Flush();
  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse::examples
