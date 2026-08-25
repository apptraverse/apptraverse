#include "threaded_room_runtime.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
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
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/directory_domain_storage.h"

#include "model/application_ids.h"
#include "model/app.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_peer_set.h"
#include "model/chat_room_local_state.h"
#include "model/client.h"
#include "model/registration.h"
#include "model/window_changed_event.h"

#include "../common/aether_p2p_transport.h"
#include "../common/aether_runtime.h"
#include "../common/chat_component.h"
#include "../common/chat_peer_schedule.h"
#include "../common/graph_builder.h"
#include "../common/room_inbound_demux.h"
#include "../common/room_membership_controller.h"
#include "../common/startup_trace.h"
#include "win_chat_presenter.h"
#include "win_window_presenter.h"
#include "windows_window.h"

namespace apptraverse::examples {
namespace {

constexpr UINT kWmModelChanged = WM_APP + 40;
constexpr auto kPresenceInterval = chat::kPresenceRefreshInterval;
constexpr auto kNetworkIdleCap = std::chrono::seconds{1};

constexpr std::uint32_t kDirtyWindow = 1u << 0;
constexpr std::uint32_t kDirtyChat = 1u << 1;
constexpr std::uint32_t kDirtyParticipants = 1u << 2;
constexpr std::uint32_t kDirtyRoomControls = 1u << 3;
constexpr std::uint32_t kDirtyAll =
    kDirtyWindow | kDirtyChat | kDirtyParticipants | kDirtyRoomControls;

using chat::ChatComponent;

bool VerboseLogEnabled() {
  char const* env = std::getenv("APPTRAVERSE_VERBOSE_LOG");
  return env != nullptr && env[0] != '\0' && env[0] != '0';
}

std::uint64_t SteadyMs() {
  using clock = std::chrono::steady_clock;
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          clock::now().time_since_epoch())
          .count());
}

std::uint64_t ThreadIdHash() {
  return static_cast<std::uint64_t>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

// Monotonic timestamp + thread id on key business/network logs.
void ThreadLog(std::string const& line) {
  std::cout << "t_ms=" << SteadyMs() << " tid=" << ThreadIdHash() << ' '
            << line << '\n';
  std::fflush(stdout);
}

void VerboseLog(std::string const& line) {
  if (VerboseLogEnabled()) {
    ThreadLog(line);
  }
}

bool IsKeyRuntimeLog(std::string const& line) {
  static constexpr char const* kPrefixes[] = {
      "UI_JOIN",
      "BUSINESS_JOIN",
      "ROOM_",
      "SYNC_",
      "LOCAL_JOIN",
      "CHAT_EVENT_FORWARDED",
      "CHAT_PENDING",
      "CHAT_PEER",
      "CHAT_SYNC",
      "CHAT_EVENT",
      "NETWORK_FRAME",
      "P2P_SESSION",
      "SMOKE_",
      "BUSINESS_SESSION_READY",
  };
  for (auto const* prefix : kPrefixes) {
    if (line.compare(0, std::char_traits<char>::length(prefix), prefix) == 0) {
      return true;
    }
  }
  return false;
}

void RuntimeLog(std::string const& line) {
  if (IsKeyRuntimeLog(line) || VerboseLogEnabled()) {
    ThreadLog(line);
  }
}

std::wstring Utf8ToWide(std::string const& utf8) {
  if (utf8.empty()) {
    return {};
  }
  int const size =
      MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
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
};

struct JoinHostCommand {
  ae::Uid uid;
};

struct WindowChangedCommand {};

struct InboundNetworkPacket {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

struct NetworkReadyEvent {};

struct StopBusinessCommand {};
struct BeginShutdownCommand {};
struct FinalizeShutdownCommand {};

struct PresenceScheduleResultCommand {
  ae::Uid peer;
  bool is_local{false};
  std::optional<chat::PeerScheduleSnapshot> result;
};

struct TransportSessionReadyCommand {
  ae::Uid peer;
  std::string source;
  std::uint64_t generation{0};
};

using BusinessItem =
    std::variant<SubmitTextCommand, JoinHostCommand, WindowChangedCommand,
                 InboundNetworkPacket, NetworkReadyEvent, StopBusinessCommand,
                 BeginShutdownCommand, FinalizeShutdownCommand,
                 PresenceScheduleResultCommand, TransportSessionReadyCommand>;

struct ConnectPeerCommand {
  ae::Uid uid;
};

struct SendSyncCommand {
  ae::Uid peer;
  ae::ObjId packet_id;
  SerializedSyncPacket bytes;
};

struct SendRawCommand {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

struct SendRoomControlCommand {
  ae::Uid peer;
  std::vector<std::uint8_t> bytes;
};

struct StopNetworkCommand {};

struct ContinuePresenceCycleCommand {};

struct SetPresenceRemoteCommand {
  ae::Uid remote_uid;
};

using NetworkItem =
    std::variant<ConnectPeerCommand, SendSyncCommand, SendRawCommand,
                 SendRoomControlCommand, StopNetworkCommand,
                 ContinuePresenceCycleCommand, SetPresenceRemoteCommand>;

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

struct CoalescedWindowBounds {
  std::mutex mu;
  bool dirty{false};
  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t dpi{96};
};

ConstructedAetherRuntime ConstructWindowsAetherApp(
    std::filesystem::path const& state_dir) {
  auto root = state_dir;
  return ConstructAetherAppWithEthernet([root]() {
    return std::make_unique<DirectoryDomainStorage>(root);
  });
}

std::int32_t CurrentSystemDpi() {
  HDC hdc = GetDC(nullptr);
  if (hdc == nullptr) {
    return 96;
  }
  int const dpi = GetDeviceCaps(hdc, LOGPIXELSX);
  ReleaseDC(nullptr, hdc);
  return dpi > 0 ? dpi : 96;
}

RECT WorkAreaForRect(RECT const& candidate) {
  HMONITOR const monitor =
      MonitorFromRect(&candidate, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
  }
  return info.rcWork;
}

RECT PrimaryWorkArea() {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  HMONITOR const primary =
      MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  if (!GetMonitorInfoW(primary, &info)) {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
  }
  return info.rcWork;
}

RECT WorkspaceToScreen(RECT workspace_rect) {
  RECT const primary_work = PrimaryWorkArea();
  OffsetRect(&workspace_rect, primary_work.left, primary_work.top);
  return workspace_rect;
}

RECT QueryNormalOuterRect(HWND hwnd) {
  if (IsIconic(hwnd) || IsZoomed(hwnd)) {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd, &placement)) {
      return WorkspaceToScreen(placement.rcNormalPosition);
    }
  }
  RECT outer{};
  GetWindowRect(hwnd, &outer);
  return outer;
}

}  // namespace

int RunThreadedRoomRuntime(ThreadedRoomCliOptions const& options) {
  if (!options.role.has_value() || options.name.empty()) {
    std::cerr << "--role and --name are required\n";
    return 1;
  }
  if (*options.role == ChatRoomRole::kHost && options.host_uid.has_value()) {
    std::cerr << "--host-uid is only valid with --role client\n";
    return 1;
  }

  RuntimeThreads().ui.store(std::this_thread::get_id(),
                            std::memory_order::relaxed);

  auto aether_name = options.aether_client_name.empty()
                         ? std::string{kWindowsAetherClientName}
                         : options.aether_client_name;

  StartupTrace("AETHER_APP_CONSTRUCT_BEGIN");
  auto runtime = ConstructWindowsAetherApp(options.state_dir);
  auto aether_app = std::move(runtime.app);
  StartupTrace("AETHER_APP_CONSTRUCT_END");
  if (aether_app.get() == nullptr) {
    std::cerr << "Failed to construct AetherApp\n";
    return 1;
  }

  StartupSelectBeginMs().store(StartupElapsedMs(), std::memory_order::relaxed);
  StartupTrace("AETHER_SELECT_BEGIN",
               ae::Format("client_name={}", aether_name));
  auto aether_client =
      SelectPersistentAetherClient(*aether_app, aether_name);
  {
    double const begin_ms =
        StartupSelectBeginMs().load(std::memory_order::relaxed);
    double const elapsed = StartupElapsedMs() - begin_ms;
    char detail[256];
    if (!aether_client) {
      std::snprintf(detail, sizeof(detail),
                    "success=0 elapsed_ms=%.1f client_source=unknown", elapsed);
      StartupTrace("AETHER_SELECT_RESULT", detail);
      std::cerr << "Failed to select Aether client\n";
      return 1;
    }
    std::snprintf(
        detail, sizeof(detail),
        "success=1 elapsed_ms=%.1f local_uid=%s "
        "client_source=unknown_api_does_not_expose_load_vs_create",
        elapsed, FormatAetherUid(aether_client->uid()).c_str());
    StartupTrace("AETHER_SELECT_RESULT", detail);
  }
  {
    StartupTrace("RX_POLICY_CONFIG_BEGIN");
    ae::ReceiveSchedule rx{};
    rx.ping_interval =
        std::chrono::duration_cast<ae::Duration>(chat::kReceivePingInterval);
    rx.receive_window =
        std::chrono::duration_cast<ae::Duration>(chat::kReceiveWindow);
    auto const ping_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(rx.ping_interval)
            .count();
    auto const window_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(rx.receive_window)
            .count();
    auto const schedule_ok = aether_client->SetReceiveSchedule(rx);
    StartupTrace(
        "RX_POLICY_CONFIG_END",
        ae::Format("ok={} ping_interval_ms={} receive_window_ms={}",
                   schedule_ok ? 1 : 0, ping_ms, window_ms));
    if (!schedule_ok) {
      std::cerr << "SetReceiveSchedule failed\n";
      return 1;
    }
    (void)aether_client->cloud_connection();
  }

  auto const local_aether_uid = FormatAetherUid(aether_client->uid());
  StartupTrace("AETHER_CLIENT_READY",
               ae::Format("uid={}", local_aether_uid));
  std::cout << "AETHER_CLIENT_READY platform=windows uid=" << local_aether_uid
            << '\n';
  std::fflush(stdout);
  if (options.print_aether_uid) {
    std::cout << "AETHER_UID=" << local_aether_uid << '\n';
    std::fflush(stdout);
    return 0;
  }

  // AppTraverse model: RAM only, rebuilt every launch.
  StartupTrace("MODEL_CREATE_BEGIN");
  auto model_storage = std::make_unique<ae::RamDomainStorage>();
  auto model_domain =
      std::make_unique<ae::Domain>(ae::Now(), *model_storage);

  auto const join_policy =
      *options.role == ChatRoomRole::kHost
          ? chat::LocalJoinPolicy::kJoinLocal
          : chat::LocalJoinPolicy::kDoNotJoinLocal;
  auto graph = BuildSingleClientChatGraph<WindowsWindow, WinWindowPresenter,
                                          WinChatPresenter>(
      *model_domain, options.name, join_policy);

  auto room_state = ChatRoomLocalState::ptr::Create(
      ae::CreateWith{*model_domain}.with_id(
          ToObjId(ApplicationObjId::ChatRoomLocalState)));
  room_state->role = *options.role;
  room_state->local_client_obj_id = graph.local_client.id().id();
  room_state->local_display_name = options.name;
  if (options.host_uid.has_value()) {
    room_state->host_uid = FormatAetherUid(*options.host_uid);
  }
  room_state->active_membership_revision = 0;
  room_state.Save();

  auto local_client = graph.local_client;
  auto chat = graph.chat;
  auto window = graph.window;

  auto& win_presenter =
      static_cast<WinWindowPresenter&>(*graph.window_presenter);
  auto& chat_ui = static_cast<WinChatPresenter&>(*graph.chat_presenter);
  StartupTrace("MODEL_CREATE_END");

  WakeQueue<BusinessItem> business_q;
  WakeQueue<NetworkItem> network_q;
  std::atomic<bool> stop{false};
  std::atomic<bool> network_stop{false};
  std::atomic<bool> ui_accepting{true};
  std::atomic<bool> network_ready{false};
  std::atomic<bool> component_stopped{false};
  std::atomic<bool> finalize_done{false};
  std::atomic<bool> shutdown_started{false};
  std::atomic<bool> shutting_down{false};
  std::atomic<bool> ui_refresh_pending{false};
  std::atomic<bool> network_wake_needed{false};
  std::atomic<std::uint32_t> ui_dirty_bits{0};
  std::atomic<ae::TaskScheduler*> scheduler{nullptr};
  std::mutex phase_mu;
  std::condition_variable phase_cv;
  std::shared_mutex model_guard;
  CoalescedWindowBounds window_bounds;

  std::thread::id ui_thread_id = std::this_thread::get_id();
  std::thread::id business_thread_id{};
  std::thread::id network_thread_id{};

  ChatComponent* live_component = nullptr;
  chat::RoomMembershipController* live_room = nullptr;
  ae::Uid live_host_uid{};

  auto AssertUiThread = [&]() {
    assert(std::this_thread::get_id() == ui_thread_id);
  };
  auto AssertBusinessThread = [&]() {
    assert(business_thread_id != std::thread::id{} &&
           std::this_thread::get_id() == business_thread_id);
  };
  auto AssertNetworkThread = [&]() {
    assert(network_thread_id != std::thread::id{} &&
           std::this_thread::get_id() == network_thread_id);
  };

  auto wake_network = [&]() {
    auto* sch = scheduler.load(std::memory_order::acquire);
    if (sch != nullptr) {
      sch->Task([]() {});
    }
    network_q.Notify();
  };

  auto request_network_wake = [&]() {
    network_wake_needed.store(true, std::memory_order::release);
  };

  auto flush_network_wake = [&]() {
    if (network_wake_needed.exchange(false, std::memory_order::acq_rel)) {
      wake_network();
    }
  };

  auto wait_flag = [&](std::atomic<bool> const& flag) {
    std::unique_lock lock{phase_mu};
    phase_cv.wait(lock, [&] { return flag.load(std::memory_order::acquire); });
  };

  auto RequestUiRefresh = [&](std::uint32_t bits) {
    if (shutting_down.load(std::memory_order::acquire)) {
      return;
    }
    ui_dirty_bits.fetch_or(bits, std::memory_order::acq_rel);
    HWND const hwnd = win_presenter.hwnd();
    if (hwnd == nullptr) {
      return;
    }
    bool expected = false;
    if (!ui_refresh_pending.compare_exchange_strong(
            expected, true, std::memory_order::acq_rel)) {
      return;
    }
    if (!PostMessageW(hwnd, kWmModelChanged, 0, 0)) {
      ui_refresh_pending.store(false, std::memory_order::release);
    }
  };

  // Call under exclusive model_guard before unlock when chat/participants dirty.
  auto EnsurePresentableLocked = [&](ChatComponent& component,
                                     std::uint32_t bits) {
    if ((bits & (kDirtyChat | kDirtyParticipants)) != 0) {
      component.EnsurePresentable();
    }
  };

  auto refresh_from_live_model = [&]() {
    AssertUiThread();
    if (shutting_down.load(std::memory_order::acquire)) {
      ui_refresh_pending.store(false, std::memory_order::release);
      return;
    }
    ui_refresh_pending.store(false, std::memory_order::release);
    std::uint32_t const bits =
        ui_dirty_bits.exchange(0, std::memory_order::acq_rel);
    if (bits == 0) {
      return;
    }
    if (!model_guard.try_lock_shared()) {
      // Business holds exclusive — re-arm dirty bits and retry.
      ui_dirty_bits.fetch_or(bits, std::memory_order::acq_rel);
      RequestUiRefresh(0);
      return;
    }
    std::shared_lock lock{model_guard, std::adopt_lock};
    if (live_component == nullptr || live_room == nullptr) {
      return;
    }
    ae::Uid host = live_host_uid;
    if (host.empty()) {
      host = live_room->host_uid();
    }
    if ((bits & kDirtyChat) != 0) {
      Trace("UI_CHAT_PRESENT_BEGIN", ae::Format("dirty={}", bits));
      Trace("MESSAGE_UI_PRESENT_BEGIN", ae::Format("dirty={}", bits));
    }
    chat_ui.PresentLive(
        chat, local_client, *live_room, host,
        [&](ae::Uid const& uid) {
          return live_component->GetPeerPresence(uid);
        },
        live_component->GetLocalPresence(), bits);
    if ((bits & kDirtyChat) != 0) {
      Trace("UI_CHAT_PRESENT_END", ae::Format("dirty={}", bits));
      Trace("MESSAGE_UI_PRESENT_END", ae::Format("dirty={}", bits));
    }
  };

  auto enqueue_window_changed = [&](std::int32_t left, std::int32_t top,
                                    std::int32_t right, std::int32_t bottom,
                                    std::int32_t dpi) {
    {
      std::scoped_lock lock{window_bounds.mu};
      window_bounds.left = left;
      window_bounds.top = top;
      window_bounds.right = right;
      window_bounds.bottom = bottom;
      window_bounds.dpi = dpi;
      window_bounds.dirty = true;
    }
    if (ui_accepting.load(std::memory_order::acquire)) {
      business_q.Push(WindowChangedCommand{});
    }
  };

  chat_ui.ConfigureRoom(
      *options.role == ChatRoomRole::kHost
          ? WinChatPresenter::RoomUiMode::kHost
          : WinChatPresenter::RoomUiMode::kClient,
      local_aether_uid);
  chat_ui.SetSubmitTextHandler([&](std::string text) {
    AssertUiThread();
    if (!ui_accepting.load(std::memory_order::acquire)) {
      return false;
    }
    business_q.Push(SubmitTextCommand{std::move(text)});
    return true;
  });
  chat_ui.SetJoinHandler([&](std::string host_text) {
    AssertUiThread();
    if (!ui_accepting.load(std::memory_order::acquire)) {
      return;
    }
    auto const uid = ae::Uid::FromString(std::string_view{host_text});
    if (uid.empty()) {
      chat_ui.SetStatusText("Invalid Host Aether ID");
      return;
    }
    Trace("UI_JOIN_CLICK", ae::Format("peer={}", uid));
    RuntimeLog(ae::Format("UI_JOIN host={}", uid));
    business_q.Push(JoinHostCommand{uid});
  });

  win_presenter.SetModelChangedMessage(kWmModelChanged);
  win_presenter.SetModelRefreshHandler(refresh_from_live_model);
  win_presenter.SetWindowChangedHandler(enqueue_window_changed);
  win_presenter.SetCommandSideEffectsEnabled(false);

  StartupTrace("WINDOW_CREATE_BEGIN");
  win_presenter.CreateNativeWindow();
  StartupTrace("WINDOW_CREATE_END");
  if (!options.title.empty() && win_presenter.hwnd() != nullptr) {
    SetWindowTextW(win_presenter.hwnd(), Utf8ToWide(options.title).c_str());
  }
  RequestUiRefresh(kDirtyAll);

  // ---- Network thread ----
  std::thread network_thread([&]() {
    network_thread_id = std::this_thread::get_id();
    RuntimeThreads().network.store(network_thread_id,
                                   std::memory_order::relaxed);
    StartupTrace("AETHER_THREAD_START");
    StartupTrace("P2P_TRANSPORT_START_BEGIN");
    AetherP2pTransport transport;
    transport.Start(*aether_app, aether_client);
    StartupTrace("P2P_TRANSPORT_START_END");
    scheduler.store(aether_app->aether()->task_scheduler.get(),
                    std::memory_order::release);

    transport.SetLogHandler([](std::string line) { RuntimeLog(line); });
    // PreWrite covers P2P frame writes only — not QueryPeerReceiveSchedule
    // cloud I/O (that path has no PreWrite hook in current API).
    transport.SetPreWriteHandler(
        [](ae::Uid const& peer, std::size_t bytes) {
          if (StartupOnceFlag(StartupFlagFirstNetworkWrite())) {
            StartupTrace(
                "FIRST_NETWORK_REQUEST_WRITE",
                ae::Format("kind=p2p_frame peer={} bytes={}", peer, bytes));
          }
        });

    transport.SetReceiveHandler(
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
          AssertNetworkThread();
          if (TryHandleP2pProbePayload(transport, peer, payload, {}, {})) {
            return;
          }
          auto const room_msg = chat::TryDecodeRoomControl(payload);
          if (room_msg.has_value()) {
            if (room_msg->type == chat::RoomControlType::kJoinRoomRequest) {
              Trace("JOIN_REQUEST_BUSINESS_ENQUEUED",
                    ae::Format("peer={} size={}", peer, payload.size()));
            } else if (room_msg->type ==
                       chat::RoomControlType::kJoinRoomAccepted) {
              Trace("JOIN_ACCEPTED_BUSINESS_ENQUEUED",
                    ae::Format("peer={} size={}", peer, payload.size()));
            }
          } else {
            Trace("MESSAGE_BUSINESS_ENQUEUED",
                  ae::Format("peer={} size={}", peer, payload.size()));
            Trace("SYNC_INITIAL_BUSINESS_ENQUEUE",
                  ae::Format("peer={} size={}", peer, payload.size()));
          }
          business_q.Push(InboundNetworkPacket{peer, payload});
        });

    transport.SetSessionReadyHandler(
        [&](ae::Uid const& peer, char const* source, std::uint64_t generation) {
          AssertNetworkThread();
          RuntimeLog(ae::Format(
              "P2P_SESSION_READY peer={} source={} generation={}", peer,
              source != nullptr ? source : "", generation));
          business_q.Push(TransportSessionReadyCommand{
              peer, source != nullptr ? std::string{source} : std::string{},
              generation});
          wake_network();
        });

    transport.SetPresenceUids(aether_client->uid(), {});
    transport.SetWakeNetwork([&] { wake_network(); });
    transport.SetScheduleResultHandler(
        [&](ae::Uid peer, bool is_local,
            std::optional<chat::PeerScheduleSnapshot> result) {
          AssertNetworkThread();
          business_q.Push(PresenceScheduleResultCommand{
              peer, is_local, std::move(result)});
          network_q.Push(ContinuePresenceCycleCommand{});
          wake_network();
        });

    network_ready.store(true, std::memory_order::release);
    business_q.Push(NetworkReadyEvent{});

    StartupTrace("AETHER_LOOP_BEGIN");
    while (!network_stop.load(std::memory_order::acquire) &&
           !aether_app->IsExited()) {
      NetworkItem item;
      while (network_q.TryPop(item)) {
        AssertNetworkThread();
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, ConnectPeerCommand>) {
                Trace("JOIN_CONNECT_PEER_DEQUEUE",
                      ae::Format("peer={}", cmd.uid));
                transport.Connect(cmd.uid);
              } else if constexpr (std::is_same_v<T, SendSyncCommand>) {
                auto const kind = TraceLookupPacketKind(cmd.packet_id.id());
                if (kind == "sync_initial") {
                  Trace("SYNC_INITIAL_NETWORK_DEQUEUE",
                        ae::Format("peer={} packet={}", cmd.peer,
                                   cmd.packet_id.id()));
                } else {
                  Trace("MESSAGE_NETWORK_DEQUEUE",
                        ae::Format("peer={} packet={}", cmd.peer,
                                   cmd.packet_id.id()));
                }
                transport.Send(cmd.peer, cmd.bytes);
              } else if constexpr (std::is_same_v<T, SendRawCommand>) {
                transport.Send(cmd.peer, cmd.bytes);
              } else if constexpr (std::is_same_v<T, SendRoomControlCommand>) {
                auto const decoded = chat::TryDecodeRoomControl(cmd.bytes);
                if (decoded.has_value() &&
                    decoded->type == chat::RoomControlType::kJoinRoomRequest) {
                  Trace("JOIN_REQUEST_NETWORK_DEQUEUE",
                        ae::Format("peer={} size={}", cmd.peer,
                                   cmd.bytes.size()));
                } else if (decoded.has_value() &&
                           decoded->type ==
                               chat::RoomControlType::kJoinRoomAccepted) {
                  Trace("JOIN_ACCEPTED_NETWORK_DEQUEUE",
                        ae::Format("peer={} size={}", cmd.peer,
                                   cmd.bytes.size()));
                }
                transport.Send(cmd.peer, cmd.bytes);
              } else if constexpr (std::is_same_v<T,
                                                   ContinuePresenceCycleCommand>) {
                transport.ContinuePresenceCycle();
              } else if constexpr (std::is_same_v<T, SetPresenceRemoteCommand>) {
                transport.SetPresenceUids(aether_client->uid(), cmd.remote_uid);
              } else if constexpr (std::is_same_v<T, StopNetworkCommand>) {
                network_stop.store(true, std::memory_order::release);
                aether_app->Exit(0);
              }
            },
            item);
        transport.PollPresence(ae::Now());
      }

      if (network_stop.load(std::memory_order::acquire) ||
          aether_app->IsExited()) {
        break;
      }

      auto const now = ae::Now();
      if (StartupOnceFlag(StartupFlagFirstAetherUpdate())) {
        StartupTrace("FIRST_AETHER_UPDATE_BEGIN");
      }
      auto const next = aether_app->Update(now);
      AssertNetworkThread();
      if (StartupOnceFlag(StartupFlagFirstAetherUpdateEnd())) {
        StartupTrace("FIRST_AETHER_UPDATE_END");
      }
      transport.Poll();
      transport.PollPresence(ae::Now());
      if (network_stop.load(std::memory_order::acquire) ||
          aether_app->IsExited()) {
        break;
      }
      // Wake paths: Aether TaskScheduler, and wake_network() → Task([]{}) +
      // network_q.Notify().
      aether_app->WaitUntil(std::min(next, ae::Now() + kNetworkIdleCap));
    }

    transport.Stop();
  });

  // ---- Business thread ----
  std::thread business_thread([&]() {
    business_thread_id = std::this_thread::get_id();
    RuntimeThreads().business.store(business_thread_id,
                                    std::memory_order::relaxed);
    StartupTrace("BUSINESS_THREAD_START");
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

    chat::ChatSyncTiming sync_timing;
    sync_timing.retry_interval = kPresenceInterval;

    auto room_log = [](std::string const& line) { RuntimeLog(line); };

    ChatComponent component(
        SyncReplica{*model_domain, *model_storage, chat.id()}, local_client,
        chat,
        [&](ae::Uid const& peer, ae::ObjId packet_id,
            SerializedSyncPacket const& bytes) {
          AssertBusinessThread();
          auto const kind = TraceLookupPacketKind(packet_id.id());
          if (kind == "sync_initial") {
            Trace("SYNC_INITIAL_NETWORK_ENQUEUE",
                  ae::Format("peer={} packet={} size={}", peer, packet_id.id(),
                             bytes.size()));
          } else {
            Trace("MESSAGE_NETWORK_ENQUEUE",
                  ae::Format("peer={} packet={} size={}", peer, packet_id.id(),
                             bytes.size()));
          }
          network_q.Push(SendSyncCommand{peer, packet_id, bytes});
          request_network_wake();
        },
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
          AssertBusinessThread();
          network_q.Push(SendRawCommand{peer, bytes});
          request_network_wake();
        },
        [&](ae::Uid const& remote_uid) {
          AssertBusinessThread();
          network_q.Push(ConnectPeerCommand{remote_uid});
          request_network_wake();
        },
        sync_timing,
        ChatComponent::LogFunction{room_log});

    chat::RoomMembershipHooks room_hooks{};
    room_hooks.send_control =
        [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
          AssertBusinessThread();
          auto const decoded = chat::TryDecodeRoomControl(bytes);
          if (decoded.has_value() &&
              decoded->type == chat::RoomControlType::kJoinRoomRequest) {
            Trace("JOIN_REQUEST_NETWORK_ENQUEUE",
                  ae::Format("peer={} size={}", peer, bytes.size()));
          } else if (decoded.has_value() &&
                     decoded->type ==
                         chat::RoomControlType::kJoinRoomAccepted) {
            Trace("JOIN_ACCEPTED_NETWORK_ENQUEUE",
                  ae::Format("peer={} size={}", peer, bytes.size()));
          }
          network_q.Push(SendRoomControlCommand{peer, bytes});
          request_network_wake();
        };
    room_hooks.connect_peer = [&](ae::Uid const& peer) {
      AssertBusinessThread();
      network_q.Push(ConnectPeerCommand{peer});
      request_network_wake();
    };
    room_hooks.add_chat_peer = [&](ae::Uid const& peer) {
      AssertBusinessThread();
      (void)component.AddPeer(peer);
      network_q.Push(SetPresenceRemoteCommand{peer});
      request_network_wake();
    };
    room_hooks.ensure_host_join =
        [&](ae::Uid const& /*uid*/, std::uint32_t client_obj_id,
            std::string const& name) -> bool {
          AssertBusinessThread();
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
              return false;
            }
          }
          auto client_base =
              Client::ptr::Create(ae::CreateWith{*model_domain});
          auto client = Client::ptr::Create(
              ae::CreateWith{*model_domain}.with_id(client_obj_id));
          client->name = name;
          client->base = client_base;
          client->CaptureBaseState();
          client.Save();
          Trace("HOST_CLIENT_OBJECT_CREATED",
                ae::Format("client_obj_id={} name={}", client_obj_id, name));
          auto join =
              JoinClientEvent::ptr::Create(ae::CreateWith{*model_domain});
          join->client = client;
          Trace("HOST_JOIN_EVENT_CREATED",
                ae::Format("event_id={} client_obj_id={}", join.id().id(),
                           client_obj_id));
          chat->Commit(join);
          chat.Save();
          Trace("HOST_JOIN_EVENT_COMMITTED",
                ae::Format("event_id={} client_obj_id={}", join.id().id(),
                           client_obj_id));
          for (auto const& record : chat->journal) {
            if (record.event.is_valid() && record.event.id() == join.id()) {
              component.PublishCommittedJournalEvent(record);
              break;
            }
          }
          return true;
        };
    room_hooks.has_local_join = [&] {
      AssertBusinessThread();
      return component.HasLocalJoin();
    };
    room_hooks.probe_local_join = [&] {
      AssertBusinessThread();
      chat::RoomLocalJoinIdentity id{};
      auto const probe = component.ProbeLocalJoin();
      id.local_client_obj_id = probe.local_client_obj_id;
      id.join_client_obj_id = probe.join_client_obj_id;
      id.obj_id_match =
          probe.kind == ChatComponent::LocalJoinMatchKind::kObjId;
      id.name_fallback =
          probe.kind == ChatComponent::LocalJoinMatchKind::kNameFallback;
      return id;
    };
    room_hooks.on_ui_changed = [&] {
      AssertBusinessThread();
      ui_dirty_bits.fetch_or(kDirtyRoomControls | kDirtyParticipants,
                             std::memory_order::acq_rel);
    };
    room_hooks.on_model_changed = [&] {
      AssertBusinessThread();
      ui_dirty_bits.fetch_or(kDirtyParticipants | kDirtyChat | kDirtyRoomControls,
                             std::memory_order::acq_rel);
    };
    room_hooks.log = room_log;

    chat::RoomMembershipController room{
        *options.role, aether_client->uid(), local_client.id().id(),
        options.name, room_state, room_hooks};

    if (*options.role == ChatRoomRole::kHost) {
      component.SetHostEventRelay(true);
    }
    component.SetIncomingPeerAuthorize(
        [&](ae::Uid const& peer) { return room.IsAuthorizedSyncPeer(peer); });
    component.SetRoomParticipantsProvider(
        [&]() -> std::vector<chat::RoomParticipantDesc> const& {
          return room.ActiveParticipants();
        });
    // Presence queries owned by network PollPresence; SetQueryPeerSchedule unused.
    component.SetLocalUid(aether_client->uid());
    if (*options.role == ChatRoomRole::kHost) {
      component.SetRoomHostUid(aether_client->uid());
      live_host_uid = aether_client->uid();
    } else if (!room.host_uid().empty()) {
      component.SetRoomHostUid(room.host_uid());
      live_host_uid = room.host_uid();
    } else if (options.host_uid.has_value()) {
      component.SetRoomHostUid(*options.host_uid);
      live_host_uid = *options.host_uid;
    }

    component.SubscribePresentationChanged([&] {
      AssertBusinessThread();
      ui_dirty_bits.fetch_or(kDirtyChat | kDirtyParticipants,
                             std::memory_order::acq_rel);
    });

    {
      std::unique_lock lock{model_guard};
      AssertBusinessThread();
      live_component = &component;
      live_room = &room;
    }

    auto mark_ui = [&](std::uint32_t bits) {
      ui_dirty_bits.fetch_or(bits, std::memory_order::acq_rel);
    };

    if (*options.role == ChatRoomRole::kHost) {
      {
        std::unique_lock lock{model_guard};
        AssertBusinessThread();
        room.HostBootstrap();
        for (auto const& p : room.ActiveParticipants()) {
          if (p.uid == aether_client->uid()) {
            continue;
          }
          network_q.Push(ConnectPeerCommand{p.uid});
          request_network_wake();
        }
        mark_ui(kDirtyAll);
      }
      flush_network_wake();
      RequestUiRefresh(0);
    } else {
      ae::Uid host{};
      if (options.host_uid.has_value()) {
        host = *options.host_uid;
      } else if (!room_state->host_uid.empty()) {
        host = ae::Uid::FromString(std::string_view{room_state->host_uid});
      }
      if (!host.empty()) {
        {
          std::unique_lock lock{model_guard};
          AssertBusinessThread();
          component.SetRoomHostUid(host);
          live_host_uid = host;
          room.ClientConnect(host);
          network_q.Push(SetPresenceRemoteCommand{host});
          request_network_wake();
          mark_ui(kDirtyAll);
        }
        flush_network_wake();
        RequestUiRefresh(0);
      }
    }

    {
      std::unique_lock lock{model_guard};
      AssertBusinessThread();
      component.Start();
      if (*options.role == ChatRoomRole::kHost) {
        for (auto const& p : room.ActiveParticipants()) {
          if (p.uid == aether_client->uid()) {
            continue;
          }
          (void)component.AddPeer(p.uid);
        }
      }
      mark_ui(kDirtyAll);
    }
    flush_network_wake();
    RequestUiRefresh(0);

    StartupTrace("BUSINESS_READY");

    auto presence_deadline =
        std::chrono::steady_clock::now() + kPresenceInterval;

    while (!stop.load(std::memory_order::acquire)) {
      auto const now_steady = std::chrono::steady_clock::now();
      auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
          presence_deadline - now_steady);
      if (wait.count() < 1) {
        wait = std::chrono::milliseconds{1};
      }
      if (wait > kPresenceInterval) {
        wait = kPresenceInterval;
      }

      BusinessItem item;
      bool const got = business_q.WaitPop(
          item, [&] { return stop.load(std::memory_order::acquire); }, wait);

      auto const now = ae::Now();
      std::uint32_t post_bits = 0;
      if (got) {
        std::visit(
            [&](auto&& cmd) {
              using T = std::decay_t<decltype(cmd)>;
              if constexpr (std::is_same_v<T, SubmitTextCommand>) {
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  if (!room.CanSendChat()) {
                    return;
                  }
                  Trace("MESSAGE_BUSINESS_DEQUEUE",
                        ae::Format("size={}", cmd.text.size()));
                  (void)component.SubmitText(std::move(cmd.text));
                  post_bits |= kDirtyChat | kDirtyParticipants;
                  EnsurePresentableLocked(component, post_bits);
                }
                flush_network_wake();
                RequestUiRefresh(post_bits);
              } else if constexpr (std::is_same_v<T, JoinHostCommand>) {
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  Trace("BUSINESS_JOIN_DEQUEUE",
                        ae::Format("peer={}", cmd.uid));
                  RuntimeLog(ae::Format("BUSINESS_JOIN host={}", cmd.uid));
                  component.SetRoomHostUid(cmd.uid);
                  live_host_uid = cmd.uid;
                  room.ClientConnect(cmd.uid);
                  network_q.Push(SetPresenceRemoteCommand{cmd.uid});
                  request_network_wake();
                  post_bits |= kDirtyAll;
                  EnsurePresentableLocked(component, post_bits);
                }
                flush_network_wake();
                RequestUiRefresh(post_bits);
              } else if constexpr (std::is_same_v<T, WindowChangedCommand>) {
                std::int32_t left = 0;
                std::int32_t top = 0;
                std::int32_t right = 0;
                std::int32_t bottom = 0;
                std::int32_t dpi = 96;
                {
                  std::scoped_lock lock{window_bounds.mu};
                  if (!window_bounds.dirty) {
                    return;
                  }
                  left = window_bounds.left;
                  top = window_bounds.top;
                  right = window_bounds.right;
                  bottom = window_bounds.bottom;
                  dpi = window_bounds.dpi;
                  window_bounds.dirty = false;
                }
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  window.Load();
                  if (!window.is_loaded()) {
                    return;
                  }
                  RECT candidate{left, top, right, bottom};
                  RECT const work = WorkAreaForRect(candidate);
                  auto event = WindowChangedEvent::ptr::Create(
                      ae::CreateWith{*model_domain});
                  event->available_left = work.left;
                  event->available_top = work.top;
                  event->available_right = work.right;
                  event->available_bottom = work.bottom;
                  event->window_left = left;
                  event->window_top = top;
                  event->window_right = right;
                  event->window_bottom = bottom;
                  event->density_dpi = dpi;
                  window->Commit(event);
                  // RAM Domain Save only — no filesystem persistence.
                  window.Save();
                  post_bits |= kDirtyWindow;
                }
                RequestUiRefresh(post_bits);
              } else if constexpr (std::is_same_v<T, InboundNetworkPacket>) {
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  std::optional<chat::RoomControlMessage> room_msg;
                  auto const room_kind =
                      chat::ClassifyRoomControlInbound(cmd.bytes, &room_msg);
                  if (room_kind == chat::RoomInboundKind::kRoomControlOk) {
                    if (room_msg->type ==
                        chat::RoomControlType::kJoinRoomRequest) {
                      Trace("JOIN_REQUEST_BUSINESS_DEQUEUE",
                            ae::Format("peer={} size={}", cmd.peer,
                                       cmd.bytes.size()));
                    } else if (room_msg->type ==
                               chat::RoomControlType::kJoinRoomAccepted) {
                      Trace("JOIN_ACCEPTED_BUSINESS_DEQUEUE",
                            ae::Format("peer={} size={}", cmd.peer,
                                       cmd.bytes.size()));
                    }
                    RuntimeLog(ae::Format("ROOM_CONTROL_RX peer={} type={}",
                                          cmd.peer,
                                          static_cast<int>(room_msg->type)));
                    room.OnControl(cmd.peer, *room_msg);
                    // Authorization may have changed — flush stashed sync.
                    component.FlushUnauthorizedIfAuthorized();
                  } else if (room_kind ==
                             chat::RoomInboundKind::kRoomControlDecodeFail) {
                    // Drop corrupt ATRM control.
                  } else {
                    Trace("MESSAGE_BUSINESS_DEQUEUE",
                          ae::Format("peer={} size={}", cmd.peer,
                                     cmd.bytes.size()));
                    component.Receive(cmd.peer, cmd.bytes);
                  }
                  // Push-based: NotifyLocalJoin after every Receive in same turn.
                  if (component.HasLocalJoin()) {
                    RuntimeLog("LOCAL_JOIN");
                    room.NotifyLocalJoinAppeared();
                  }
                  if (!room.host_uid().empty()) {
                    component.SetRoomHostUid(room.host_uid());
                    live_host_uid = room.host_uid();
                  }
                  post_bits |= kDirtyChat | kDirtyParticipants | kDirtyRoomControls;
                  EnsurePresentableLocked(component, post_bits);
                }
                flush_network_wake();
                RequestUiRefresh(post_bits);
              } else if constexpr (std::is_same_v<T,
                                                   PresenceScheduleResultCommand>) {
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  if (cmd.is_local) {
                    component.OnLocalScheduleResult(std::move(cmd.result));
                  } else {
                    component.OnPeerScheduleResult(cmd.peer,
                                                   std::move(cmd.result));
                  }
                  post_bits |= kDirtyParticipants;
                  EnsurePresentableLocked(component, post_bits);
                }
                flush_network_wake();
                RequestUiRefresh(post_bits);
              } else if constexpr (std::is_same_v<T,
                                                   TransportSessionReadyCommand>) {
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  RuntimeLog(ae::Format(
                      "BUSINESS_SESSION_READY peer={} source={} generation={}",
                      cmd.peer, cmd.source, cmd.generation));
                  // No last_control resend — transport FIFO flush only.
                  room.OnTransportSessionReady(cmd.peer);
                  post_bits |= kDirtyRoomControls;
                }
                flush_network_wake();
                RequestUiRefresh(post_bits);
              } else if constexpr (std::is_same_v<T, NetworkReadyEvent>) {
                // Boot only.
              } else if constexpr (std::is_same_v<T, BeginShutdownCommand>) {
                {
                  std::unique_lock lock{model_guard};
                  AssertBusinessThread();
                  component.Stop();
                  live_component = nullptr;
                  live_room = nullptr;
                }
                component_stopped.store(true, std::memory_order::release);
                phase_cv.notify_all();
              } else if constexpr (std::is_same_v<T, FinalizeShutdownCommand>) {
                finalize_done.store(true, std::memory_order::release);
                phase_cv.notify_all();
                stop.store(true, std::memory_order::release);
              } else if constexpr (std::is_same_v<T, StopBusinessCommand>) {
                stop.store(true, std::memory_order::release);
              }
            },
            item);
      }

      std::uint32_t tick_bits = 0;
      bool smoke_sent = false;
      {
        std::unique_lock lock{model_guard};
        AssertBusinessThread();
        if (live_component != nullptr) {
          component.Tick(now);
          room.Tick(now);
          if (room.ui_status() == chat::RoomUiStatus::kWaitingForOwnJoin &&
              component.HasLocalJoin()) {
            room.NotifyLocalJoinAppeared();
            tick_bits |= kDirtyRoomControls | kDirtyParticipants | kDirtyChat;
          }
          if (options.send_after_active.has_value() && room.CanSendChat()) {
            static bool sent_once = false;
            bool peers_ready = false;
            if (*options.role == ChatRoomRole::kHost) {
              peers_ready = room.ActiveParticipants().size() > 1;
              if (peers_ready) {
                for (auto const& p : room.ActiveParticipants()) {
                  if (p.uid == aether_client->uid()) {
                    continue;
                  }
                  if (!component.IsPeerInitialSyncComplete(p.uid)) {
                    peers_ready = false;
                    break;
                  }
                }
              }
            } else {
              peers_ready = !room.host_uid().empty() &&
                            component.IsPeerInitialSyncComplete(room.host_uid());
            }
            if (!sent_once && peers_ready) {
              sent_once = true;
              smoke_sent = true;
              // Soak: five outbound messages (H1..H5 / C1..C5 style prefixes).
              for (int i = 1; i <= 5; ++i) {
                auto const text =
                    *options.send_after_active + std::to_string(i);
                (void)component.SubmitText(text);
                std::cout << "SMOKE_SEND text=" << text << '\n';
              }
              tick_bits |= kDirtyChat | kDirtyParticipants;
              std::fflush(stdout);
            }
          }
          tick_bits |=
              ui_dirty_bits.load(std::memory_order::acquire) &
              (kDirtyChat | kDirtyParticipants | kDirtyRoomControls);
          EnsurePresentableLocked(component, tick_bits);
        }
      }
      flush_network_wake();
      if (tick_bits != 0 || smoke_sent) {
        RequestUiRefresh(tick_bits);
      }

      if (std::chrono::steady_clock::now() >= presence_deadline) {
        presence_deadline =
            std::chrono::steady_clock::now() + kPresenceInterval;
      }
    }

    if (!component_stopped.load(std::memory_order::acquire)) {
      std::unique_lock lock{model_guard};
      AssertBusinessThread();
      component.Stop();
      live_component = nullptr;
      live_room = nullptr;
    }
  });

  // ---- UI thread message loop ----
  StartupTrace("UI_MESSAGE_LOOP_BEGIN");
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  bool expected = false;
  if (!shutdown_started.compare_exchange_strong(expected, true)) {
    return static_cast<int>(msg.wParam);
  }

  // Ordered shutdown: ignore further WM_APP_MODEL_CHANGED; stop business;
  // stop network; join; then destroy RAM model (scope exit).
  shutting_down.store(true, std::memory_order::release);
  ui_accepting.store(false, std::memory_order::release);
  business_q.Push(BeginShutdownCommand{});
  wait_flag(component_stopped);

  network_q.Push(StopNetworkCommand{});
  wake_network();
  if (network_thread.joinable()) {
    network_thread.join();
  }
  scheduler.store(nullptr, std::memory_order::release);

  business_q.Push(FinalizeShutdownCommand{});
  wait_flag(finalize_done);
  if (business_thread.joinable()) {
    business_thread.join();
  }

  // Destroy RAM model only after both worker threads have joined.
  live_component = nullptr;
  live_room = nullptr;
  model_domain.reset();
  model_storage.reset();

  return static_cast<int>(msg.wParam);
}

}  // namespace apptraverse::examples
