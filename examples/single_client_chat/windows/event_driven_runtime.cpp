#include "event_driven_runtime.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
#include "../common/chat_transcript.h"
#include "../common/graph_builder.h"
#include "latency_trace.h"
#include "win_add_peer_dialog.h"
#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "windows_window.h"

namespace apptraverse::examples {
namespace {

constexpr UINT kWmPresentation = WM_APP + 61;
constexpr auto kBusinessIdleCap = std::chrono::milliseconds{100};
constexpr auto kNetworkIdleCap = std::chrono::seconds{1};

using chat::ChatComponent;
using chat::ChatPresentationSnapshot;

std::int64_t UtcMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
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

struct StopBusinessCommand {};
struct BeginShutdownCommand {};
struct FinalizeShutdownCommand {};

using BusinessItem =
    std::variant<SubmitTextCommand, AddPeerCommand, InboundNetworkPacket,
                 NetworkReadyEvent, StopBusinessCommand, BeginShutdownCommand,
                 FinalizeShutdownCommand>;

struct ConnectPeerCommand {
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

struct StopNetworkCommand {};

using NetworkItem =
    std::variant<ConnectPeerCommand, SendSyncCommand, SendRawCommand,
                 StopNetworkCommand>;

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
  using PresentationFn = std::function<void(ChatPresentationSnapshot const&)>;

  void SetHandlers(SubmitFn submit, AddPeerFn add_peer,
                   std::string local_uid, LatencyTrace* trace) {
    submit_ = std::move(submit);
    add_peer_ = std::move(add_peer);
    local_uid_ = std::move(local_uid);
    trace_ = trace;
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
        }
        return 0;
      case kWmPresentation: {
        std::unique_ptr<ChatPresentationSnapshot> snap(
            reinterpret_cast<ChatPresentationSnapshot*>(lparam));
        ApplySnapshot(*snap);
        return 0;
      }
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
  }

  void CreateControls(HWND parent) {
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
    add_ = CreateWindowExW(
        0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4)),
        GetModuleHandleW(nullptr), nullptr);
  }

  void Layout(int width, int height) {
    int const margin = 8;
    int const edit_h = 28;
    int const send_w = 80;
    int const add_w = 36;
    int const bottom = height - margin - edit_h;
    int const transcript_h = bottom - margin;
    if (transcript_ != nullptr) {
      MoveWindow(transcript_, margin, margin, width - 2 * margin,
                 transcript_h > 0 ? transcript_h : 0, TRUE);
    }
    int const edit_w = width - 4 * margin - send_w - add_w;
    if (edit_ != nullptr) {
      MoveWindow(edit_, margin, bottom, edit_w > 0 ? edit_w : 0, edit_h, TRUE);
    }
    if (send_ != nullptr) {
      MoveWindow(send_, width - 2 * margin - add_w - send_w, bottom, send_w,
                 edit_h, TRUE);
    }
    if (add_ != nullptr) {
      MoveWindow(add_, width - margin - add_w, bottom, add_w, edit_h, TRUE);
    }
  }

  void OnSend() {
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
    if (local_uid_.empty() || !add_peer_) {
      return;
    }
    ShowAddPeerDialog(hwnd_, local_uid_, add_peer_);
  }

  void ApplySnapshot(ChatPresentationSnapshot const& snapshot) {
    if (transcript_ == nullptr) {
      return;
    }
    auto const utf8 = FormatChatPresentationUtf8(snapshot);
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
  std::string local_uid_;
  SubmitFn submit_;
  AddPeerFn add_peer_;
  PresentationFn on_applied_;
  LatencyTrace* trace_{nullptr};
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
  std::filesystem::remove_all(options.state_dir);
  auto const model_root = ModelRoot(options.state_dir);
  auto const aether_root = AetherRoot(options.state_dir);
  std::filesystem::create_directories(model_root);
  std::filesystem::create_directories(aether_root);

  ae::RamDomainStorage model_ram;
  ae::Domain model_domain{ae::Now(), model_ram};
  auto graph = BuildSingleClientChatGraph<WindowsWindow, WinWindowPresenter,
                                          WinChatPresenter>(model_domain,
                                                            "Windows");
  graph.app.Save();
  graph.chat.Save();
  graph.peer_set.Save();
  graph.local_client.Save();
  if (graph.window.is_valid()) {
    graph.window.Save();
  }
  SaveDirectorySnapshot(model_ram, model_root);

  // Touch Aether storage so the empty root exists for the first SelectClient.
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
            << " name=" << graph.local_client->name << '\n';
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
    std::cout << marker << " t_us=" << UtcMicros() << '\n';
    std::fflush(stdout);
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
    SetDomainSnapshotMarkerSink({});
    return 0;
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

  WakeQueue<BusinessItem> business_q;
  WakeQueue<NetworkItem> network_q;
  std::atomic<bool> stop{false};
  std::atomic<bool> ui_accepting{true};
  std::atomic<bool> network_ready{false};
  std::atomic<bool> component_stopped{false};
  std::atomic<bool> finalize_done{false};
  std::atomic<ae::TaskScheduler*> scheduler{nullptr};
  std::mutex phase_mu;
  std::condition_variable phase_cv;

  auto wake_network = [&]() {
    auto* sch = scheduler.load(std::memory_order::acquire);
    if (sch != nullptr) {
      sch->Task([]() {});
    }
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

  // ---- Network thread ----
  std::thread network_thread([&]() {
    AetherP2pTransport transport;
    transport.Start(*aether_app, aether_client);
    scheduler.store(aether_app->aether()->task_scheduler.get(),
                    std::memory_order::release);

    transport.SetPreWriteHandler(
        [&](ae::Uid const& /*peer*/, std::size_t /*frame_bytes*/) {
          trace.Mark(TraceThreadRole::kNetwork,
                     LatencyTrace::Marker::kP2pWriteCalled,
                     pending_write.text_key.empty()
                         ? nullptr
                         : pending_write.text_key.c_str(),
                     pending_write.event_id, pending_write.packet_id);
        });

    transport.SetReceiveHandler(
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
          if (TryHandleP2pProbePayload(transport, peer, payload, {}, {})) {
            return;
          }
          trace.Mark(TraceThreadRole::kNetwork,
                     LatencyTrace::Marker::kRemoteP2pReceived);
          business_q.Push(InboundNetworkPacket{peer, payload});
        });

    network_ready.store(true, std::memory_order::release);
    business_q.Push(NetworkReadyEvent{});

    while (!stop.load(std::memory_order::acquire) && !aether_app->IsExited()) {
      NetworkItem item;
      while (network_q.TryPop(item)) {
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, ConnectPeerCommand>) {
                transport.Connect(cmd.uid);
              } else if constexpr (std::is_same_v<T, SendSyncCommand>) {
                pending_write.text_key = cmd.text_key;
                pending_write.event_id = cmd.event_id;
                pending_write.packet_id = cmd.packet_id.id();
                transport.Send(cmd.peer, cmd.bytes);
                pending_write = {};
              } else if constexpr (std::is_same_v<T, SendRawCommand>) {
                transport.Send(cmd.peer, cmd.bytes);
              } else if constexpr (std::is_same_v<T, StopNetworkCommand>) {
                stop.store(true, std::memory_order::release);
              }
            },
            item);
      }

      auto const now = ae::Now();
      auto const next = aether_app->Update(now);
      if (stop.load(std::memory_order::acquire) || aether_app->IsExited()) {
        break;
      }
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
          std::cout << line << " t_us=" << UtcMicros() << '\n';
          std::fflush(stdout);
          trace.MarkFromProductLine(TraceThreadRole::kBusiness, line);
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

    component.Start();
    publish_presentation(component);

    if (options.peer.has_value()) {
      component.AddPeer(*options.peer);
      publish_presentation(component);
    }

    auto maybe_peer_inbox = [&]() {
      if (!options.peer_inbox.has_value()) {
        return;
      }
      auto const& inbox = *options.peer_inbox;
      std::error_code ec;
      if (!std::filesystem::exists(inbox, ec) || ec) {
        return;
      }
      std::ifstream in(inbox, std::ios::binary);
      if (!in) {
        return;
      }
      std::string line;
      if (!std::getline(in, line)) {
        in.close();
        std::filesystem::remove(inbox, ec);
        return;
      }
      in.close();
      std::filesystem::remove(inbox, ec);
      while (!line.empty() &&
             (line.back() == char(13) || line.back() == char(10))) {
        line.pop_back();
      }
      if (line.empty()) {
        return;
      }
      auto const uid = ae::Uid::FromString(std::string_view{line});
      if (uid.empty()) {
        return;
      }
      component.AddPeer(uid);
      publish_presentation(component);
      std::cout << "CHAT_PEER_INBOX_ADDED uid=" << FormatAetherUid(uid)
                << " t_us=" << UtcMicros() << '\n';
      std::fflush(stdout);
    };

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
                  std::cout << "CHAT_MESSAGE_COMMITTED platform=windows event="
                            << *event_id << " text_key=" << cmd.text_key
                            << " t_us=" << UtcMicros() << '\n';
                  std::fflush(stdout);
                }
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, AddPeerCommand>) {
                component.AddPeer(cmd.uid);
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, InboundNetworkPacket>) {
                component.Receive(cmd.peer, cmd.bytes);
                publish_presentation(component);
              } else if constexpr (std::is_same_v<T, NetworkReadyEvent>) {
                // already handled at boot
              } else if constexpr (std::is_same_v<T, BeginShutdownCommand>) {
                // Drain already-accepted UI/network commands already in queue by
                // continuing the loop; Stop chat so presence/offline can enqueue.
                packet_trace_by_id.clear();
                component.Stop();
                component_stopped.store(true, std::memory_order::release);
                phase_cv.notify_all();
              } else if constexpr (std::is_same_v<T, FinalizeShutdownCommand>) {
                // Inbound packets queued before network join are drained by
                // continuing WaitPop until this command; then finalize RAM.
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
          std::cout << "CHAT_MESSAGE_VISIBLE platform=windows text_key="
                    << item_view.text << " t_us=" << UtcMicros() << '\n';
          std::fflush(stdout);
        }
      }
    }

    if (!component_stopped.load(std::memory_order::acquire)) {
      component.Stop();
    }
  });

  // ---- UI thread (this thread) ----
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

  std::wstring title = L"AppTraverse Chat";
  if (!options.window_title_suffix.empty()) {
    title += L" [";
    title += Utf8ToWide(options.window_title_suffix);
    title += L"]";
  }
  ui.Create(title);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // Ordered shutdown: UI closed → stop component → network join → drain →
  // final RAM serialize → one directory snapshot → destroy model.
  ui_accepting.store(false, std::memory_order::release);
  business_q.Push(BeginShutdownCommand{});
  wait_flag(component_stopped);

  network_q.Push(StopNetworkCommand{});
  wake_network();
  aether_app->Exit(0);
  if (network_thread.joinable()) {
    network_thread.join();
  }

  business_q.Push(FinalizeShutdownCommand{});
  wait_flag(finalize_done);
  if (business_thread.joinable()) {
    business_thread.join();
  }

  SaveDirectorySnapshot(*model_storage, model_root);

  model_domain.reset();
  model_storage.reset();

  SetDomainSnapshotMarkerSink({});
  trace.Flush();
  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse::examples
