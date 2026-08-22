// Headless Host + Client runtime tests: the real chat business stack in one
// process, no Win32 UI, no threads, no Aether transport.
//
// All timing comes from the in-memory HeadlessTrace; the only stdout output is
// the pass/fail lines and the final timing summary.
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "headless_room_runtime.h"

namespace {

using apptraverse::ChatRoomRole;
using apptraverse::chat::RoomUiStatus;
using apptraverse::testing::HeadlessMemoryTransport;
using apptraverse::testing::HeadlessRoomRuntime;
using apptraverse::testing::HeadlessTrace;
using apptraverse::testing::PumpFor;
using apptraverse::testing::PumpUntil;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

constexpr auto kActivationTimeout = std::chrono::milliseconds{5000};
constexpr auto kDeliveryTimeout = std::chrono::milliseconds{5000};
constexpr auto kSettleTime = std::chrono::milliseconds{300};

ae::Uid MakeUid(std::uint8_t fill) {
  std::array<std::uint8_t, ae::Uid::kSize> bytes{};
  bytes.fill(fill);
  return ae::Uid{bytes};
}

std::filesystem::path MakeRunRoot(std::string const& tag) {
  auto const stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  auto root = std::filesystem::temp_directory_path() /
              ("headless-room-" + tag + "-" + std::to_string(stamp));
  std::filesystem::create_directories(root);
  return root;
}

bool HasLine(HeadlessRoomRuntime& runtime, std::string const& line) {
  auto const lines = runtime.Presenter().Lines();
  return std::find(lines.begin(), lines.end(), line) != lines.end();
}

// Elapsed milliseconds between two trace markers; -1 when either is missing.
std::int64_t Span(HeadlessTrace const& trace, std::string_view from_role,
                  std::string_view from_marker, std::string_view to_role,
                  std::string_view to_marker, std::int64_t since_us) {
  auto const from = trace.FirstAfter(from_role, from_marker, since_us);
  if (!from.has_value()) {
    return -1;
  }
  auto const to = trace.FirstAfter(to_role, to_marker, *from);
  if (!to.has_value()) {
    return -1;
  }
  return (*to - *from) / 1000;
}

struct Timings {
  std::int64_t host_activation_ms{-1};
  std::int64_t client_activation_ms{-1};
  std::int64_t host_commit_to_client_presentation_ms{-1};
  std::int64_t client_commit_to_host_presentation_ms{-1};
  std::int64_t restart_to_session_ready_ms{-1};
  std::int64_t session_ready_to_write_ms{-1};
  std::int64_t write_to_host_apply_ms{-1};
  std::int64_t restart_to_pending_delivery_ms{-1};
  std::int64_t pending_cleared_ms{-1};
};

Timings g_timings{};

// ---------------------------------------------------------------------------
// Test 1: activation and bidirectional messages
// ---------------------------------------------------------------------------

void TestHostClientActivationAndMessages() {
  auto const root = MakeRunRoot("activation");
  HeadlessTrace trace;
  HeadlessMemoryTransport link{trace};

  HeadlessRoomRuntime host{root / "host", link, trace, "host"};
  HeadlessRoomRuntime client{root / "client", link, trace, "client"};
  std::vector<HeadlessRoomRuntime*> all{&host, &client};

  auto const host_uid = MakeUid(0x11);
  auto const client_uid = MakeUid(0x22);

  host.StartHost("HostUser", host_uid);
  CHECK(host.Uid() == host_uid);
  CHECK(host.Room()->ui_status() == RoomUiStatus::kActive);
  CHECK(host.Presenter().SendEnabled());
  CHECK(host.Presenter().JoinCount() == 1);
  CHECK(HasLine(host, "* HostUser joined"));

  // The Client knows the host UID from its own state, so Start() connects.
  client.StartClient("ClientUser", client_uid, host_uid);
  CHECK(client.Uid() == client_uid);
  CHECK(client.Room()->ui_status() != RoomUiStatus::kActive);
  CHECK(!client.Presenter().SendEnabled());

  CHECK(PumpUntil(
      link, all,
      [&] {
        return client.Room()->ui_status() == RoomUiStatus::kActive &&
               host.Room()->ui_status() == RoomUiStatus::kActive &&
               client.Presenter().JoinCount() == 2 &&
               host.Presenter().JoinCount() == 2;
      },
      kActivationTimeout));

  g_timings.host_activation_ms =
      Span(trace, "host", "PROCESS_START", "host", "ROOM_ACTIVE_CHANGED", 0);
  g_timings.client_activation_ms =
      Span(trace, "client", "PROCESS_START", "client", "ROOM_ACTIVE_CHANGED", 0);

  CHECK(client.Presenter().SendEnabled());
  CHECK(host.Presenter().SendEnabled());
  CHECK(client.Presenter().RoomStatus() == RoomUiStatus::kActive);
  CHECK(host.Presenter().RoomStatus() == RoomUiStatus::kActive);
  CHECK(host.Room()->applied_revision() == 2);
  CHECK(client.Room()->applied_revision() == 2);

  // Transcript on both sides: Host join, then Client join, once each.
  CHECK(HasLine(host, "* HostUser joined"));
  CHECK(HasLine(host, "* ClientUser joined"));
  CHECK(HasLine(client, "* HostUser joined"));
  CHECK(HasLine(client, "* ClientUser joined"));
  CHECK(client.Presenter().Participants().size() == 2);
  CHECK(host.Presenter().Participants().size() == 2);

  auto const t_host_send = HeadlessTrace::NowUs();
  CHECK(host.Send("HEADLESS_HOST_MESSAGE"));
  CHECK(PumpUntil(
      link, all,
      [&] {
        return client.Presenter().CountMessage("HEADLESS_HOST_MESSAGE") == 1;
      },
      kDeliveryTimeout));
  trace.Event("client", "MESSAGE_VISIBLE", "HEADLESS_HOST_MESSAGE");
  g_timings.host_commit_to_client_presentation_ms =
      Span(trace, "host", "EVENT_COMMITTED", "client", "MESSAGE_VISIBLE",
           t_host_send);

  auto const t_client_send = HeadlessTrace::NowUs();
  CHECK(client.Send("HEADLESS_CLIENT_MESSAGE"));
  CHECK(PumpUntil(
      link, all,
      [&] {
        return host.Presenter().CountMessage("HEADLESS_CLIENT_MESSAGE") == 1;
      },
      kDeliveryTimeout));
  trace.Event("host", "MESSAGE_VISIBLE", "HEADLESS_CLIENT_MESSAGE");
  g_timings.client_commit_to_host_presentation_ms =
      Span(trace, "client", "EVENT_COMMITTED", "host", "MESSAGE_VISIBLE",
           t_client_send);

  // Retries must not duplicate anything.
  PumpFor(link, all, kSettleTime);
  for (auto* runtime : all) {
    CHECK(runtime->Presenter().CountMessage("HEADLESS_HOST_MESSAGE") == 1);
    CHECK(runtime->Presenter().CountMessage("HEADLESS_CLIENT_MESSAGE") == 1);
    CHECK(runtime->Presenter().JoinCount() == 2);
    CHECK(runtime->Presenter().PendingCount() == 0);
  }

  host.Stop();
  client.Stop();
  std::filesystem::remove_all(root);
  std::cout << "HeadlessRoom.HostClientActivationAndMessages OK\n";
}

// ---------------------------------------------------------------------------
// Test 2: pending message delivered after the Host restarts
// ---------------------------------------------------------------------------

void TestPendingMessageAfterReconnect() {
  auto const root = MakeRunRoot("pending");
  HeadlessTrace trace;
  HeadlessMemoryTransport link{trace};

  HeadlessRoomRuntime host{root / "host", link, trace, "host"};
  HeadlessRoomRuntime client{root / "client", link, trace, "client"};

  auto const host_uid = MakeUid(0x33);
  auto const client_uid = MakeUid(0x44);

  host.StartHost("HostUser", host_uid);
  client.StartClient("ClientUser", client_uid, host_uid);
  std::vector<HeadlessRoomRuntime*> all{&host, &client};
  CHECK(PumpUntil(
      link, all,
      [&] {
        return client.Room()->ui_status() == RoomUiStatus::kActive &&
               client.Presenter().JoinCount() == 2 &&
               host.Presenter().JoinCount() == 2;
      },
      kActivationTimeout));
  CHECK(client.Presenter().PendingCount() == 0);

  // The Host process goes away; the Client keeps running.
  host.Stop();
  std::vector<HeadlessRoomRuntime*> client_only{&client};

  CHECK(client.Send("OFFLINE_MESSAGE"));
  PumpFor(link, client_only, std::chrono::milliseconds{200});
  CHECK(client.Presenter().CountMessage("OFFLINE_MESSAGE") == 1);
  CHECK(client.Presenter().PendingCount() > 0);
  CHECK(client.Presenter().SendEnabled());
  CHECK(host.Presenter().CountMessage("OFFLINE_MESSAGE") == 0);

  // Host restarts from the same state directory. No Send, no Connect, no UI.
  auto const t_restart = HeadlessTrace::NowUs();
  host.Start();
  CHECK(PumpUntil(
      link, all,
      [&] { return host.Presenter().CountMessage("OFFLINE_MESSAGE") == 1; },
      kDeliveryTimeout));
  trace.Event("host", "MESSAGE_VISIBLE", "OFFLINE_MESSAGE");

  CHECK(PumpUntil(
      link, all, [&] { return client.Presenter().PendingCount() == 0; },
      kDeliveryTimeout));
  trace.Event("client", "PENDING_ZERO", {});

  g_timings.restart_to_session_ready_ms =
      Span(trace, "host", "PROCESS_START", "client", "SESSION_READY",
           t_restart);
  g_timings.session_ready_to_write_ms =
      Span(trace, "client", "SESSION_READY", "client", "SYNC_WRITE", t_restart);
  g_timings.write_to_host_apply_ms =
      Span(trace, "client", "SYNC_WRITE", "host", "SYNC_APPLY", t_restart);
  g_timings.restart_to_pending_delivery_ms =
      Span(trace, "host", "PROCESS_START", "host", "MESSAGE_VISIBLE",
           t_restart);
  g_timings.pending_cleared_ms =
      Span(trace, "host", "PROCESS_START", "client", "PENDING_ZERO",
           t_restart);

  PumpFor(link, all, kSettleTime);
  CHECK(host.Presenter().CountMessage("OFFLINE_MESSAGE") == 1);
  CHECK(client.Presenter().CountMessage("OFFLINE_MESSAGE") == 1);
  CHECK(client.Presenter().PendingCount() == 0);
  CHECK(host.Presenter().PendingCount() == 0);
  CHECK(host.Presenter().JoinCount() == 2);
  CHECK(client.Presenter().JoinCount() == 2);
  CHECK(host.Room()->applied_revision() == 2);
  CHECK(client.Room()->applied_revision() == 2);
  CHECK(client.Presenter().SendEnabled());
  CHECK(client.Room()->ui_status() == RoomUiStatus::kActive);

  host.Stop();
  client.Stop();
  std::filesystem::remove_all(root);
  std::cout << "HeadlessRoom.PendingMessageAfterReconnect OK\n";
}

// ---------------------------------------------------------------------------
// Test 3: restart over the same state directories keeps identity
// ---------------------------------------------------------------------------

void TestRestartRestoresIdentity() {
  auto const root = MakeRunRoot("identity");
  HeadlessTrace trace;
  HeadlessMemoryTransport link{trace};

  auto const host_uid = MakeUid(0x55);
  auto const client_uid = MakeUid(0x66);
  auto const host_dir = root / "host";
  auto const client_dir = root / "client";

  std::uint32_t host_client_obj_id = 0;
  std::uint32_t client_client_obj_id = 0;

  {
    HeadlessRoomRuntime host{host_dir, link, trace, "host"};
    HeadlessRoomRuntime client{client_dir, link, trace, "client"};
    host.StartHost("HostUser", host_uid);
    client.StartClient("ClientUser", client_uid, host_uid);
    std::vector<HeadlessRoomRuntime*> all{&host, &client};
    CHECK(PumpUntil(
        link, all,
        [&] {
          return client.Room()->ui_status() == RoomUiStatus::kActive &&
                 client.Presenter().JoinCount() == 2 &&
                 host.Presenter().JoinCount() == 2;
        },
        kActivationTimeout));
    host_client_obj_id = host.LocalClientObjId();
    client_client_obj_id = client.LocalClientObjId();
    CHECK(host.Role() == ChatRoomRole::kHost);
    CHECK(client.Role() == ChatRoomRole::kClient);
    host.Stop();
    client.Stop();
  }

  // Fresh runtime objects over the same state directories.
  HeadlessRoomRuntime host2{host_dir, link, trace, "host"};
  HeadlessRoomRuntime client2{client_dir, link, trace, "client"};
  host2.Start();
  client2.Start();
  std::vector<HeadlessRoomRuntime*> all2{&host2, &client2};

  CHECK(host2.Uid() == host_uid);
  CHECK(client2.Uid() == client_uid);
  CHECK(host2.Role() == ChatRoomRole::kHost);
  CHECK(client2.Role() == ChatRoomRole::kClient);
  CHECK(host2.LocalName() == "HostUser");
  CHECK(client2.LocalName() == "ClientUser");
  CHECK(host2.LocalClientObjId() == host_client_obj_id);
  CHECK(client2.LocalClientObjId() == client_client_obj_id);
  // Host is Active from its persisted revision, without a new activation.
  CHECK(host2.Room()->ui_status() == RoomUiStatus::kActive);
  CHECK(host2.Room()->applied_revision() == 2);
  CHECK(host2.Presenter().JoinCount() == 2);

  CHECK(PumpUntil(
      link, all2,
      [&] {
        return client2.Room()->ui_status() == RoomUiStatus::kActive &&
               client2.Presenter().JoinCount() == 2;
      },
      kActivationTimeout));
  PumpFor(link, all2, kSettleTime);

  // Join count must not grow on restart, and the room stays at revision 2.
  CHECK(host2.Presenter().JoinCount() == 2);
  CHECK(client2.Presenter().JoinCount() == 2);
  CHECK(host2.Room()->applied_revision() == 2);
  CHECK(client2.Room()->applied_revision() == 2);
  CHECK(client2.Presenter().SendEnabled());
  CHECK(host2.Presenter().Participants().size() == 2);
  CHECK(client2.Presenter().Participants().size() == 2);

  host2.Stop();
  client2.Stop();
  std::filesystem::remove_all(root);
  std::cout << "HeadlessRoom.RestartRestoresIdentity OK\n";
}

}  // namespace

int main() {
  TestHostClientActivationAndMessages();
  TestPendingMessageAfterReconnect();
  TestRestartRestoresIdentity();

  std::cout << "TIMING activation host_ms=" << g_timings.host_activation_ms
            << " client_ms=" << g_timings.client_activation_ms << '\n';
  std::cout << "TIMING commit_to_presentation host_to_client_ms="
            << g_timings.host_commit_to_client_presentation_ms
            << " client_to_host_ms="
            << g_timings.client_commit_to_host_presentation_ms << '\n';
  std::cout << "TIMING reconnect restart_to_session_ready_ms="
            << g_timings.restart_to_session_ready_ms
            << " session_ready_to_write_ms="
            << g_timings.session_ready_to_write_ms
            << " write_to_host_apply_ms=" << g_timings.write_to_host_apply_ms
            << " restart_to_delivery_ms="
            << g_timings.restart_to_pending_delivery_ms
            << " pending_cleared_ms=" << g_timings.pending_cleared_ms << '\n';
  std::cout << "headless_room_runtime_test OK\n";
  return 0;
}
