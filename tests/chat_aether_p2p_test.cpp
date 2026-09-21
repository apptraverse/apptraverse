#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/link.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_node.h"
#include "apptraverse/shared_sync_runtime.h"

#include "aether_byte_transport.h"
#include "chat_aether_runtime.h"
#include "chat_commands.h"
#include "chat_model.h"

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

namespace {

using apptraverse::example::chat_demo::AetherByteTransport;
using apptraverse::example::chat_demo::ChatAetherRuntime;
using apptraverse::example::chat_demo::ChatEntry;
using apptraverse::example::chat_demo::ChatRoom;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::MessageAddedEvent;
using apptraverse::example::chat_demo::OpenOrSelectChat;
using apptraverse::example::chat_demo::PeerPresence;
using apptraverse::example::chat_demo::SubmitDraft;

class ManualModelDispatcher {
 public:
  void Post(std::function<void()> task) {
    std::lock_guard<std::mutex> lock{mu_};
    tasks_.push_back(std::move(task));
  }

  void Drain() {
    std::vector<std::function<void()>> to_run;
    {
      std::lock_guard<std::mutex> lock{mu_};
      to_run.swap(tasks_);
    }
    for (auto& task : to_run) {
      if (task) {
        task();
      }
    }
  }

 private:
  std::mutex mu_;
  std::vector<std::function<void()>> tasks_;
};

struct Process {
  pid_t pid{-1};
  int in_fd{-1};
  int out_fd{-1};
  std::string read_buffer;

  bool is_alive() const {
    if (pid <= 0) {
      return false;
    }
    int status = 0;
    pid_t res = ::waitpid(pid, &status, WNOHANG);
    return res == 0;
  }

  void WriteLine(std::string const& str) {
    std::string data = str + "\n";
    std::size_t written = 0;
    while (written < data.size()) {
      ssize_t n = ::write(in_fd, data.data() + written, data.size() - written);
      if (n > 0) {
        written += static_cast<std::size_t>(n);
      } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        continue;
      } else {
        break;
      }
    }
  }

  std::string ReadLine(int timeout_ms = 30000) {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      auto nl = read_buffer.find('\n');
      if (nl != std::string::npos) {
        std::string line = read_buffer.substr(0, nl);
        read_buffer.erase(0, nl + 1);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        return line;
      }

      auto const now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return "";
      }
      auto const rem_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - now)
                              .count();

      struct pollfd pfd;
      pfd.fd = out_fd;
      pfd.events = POLLIN;
      pfd.revents = 0;

      int ret = ::poll(&pfd, 1, static_cast<int>(rem_ms));
      if (ret > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
        char buf[512];
        ssize_t n = ::read(out_fd, buf, sizeof(buf));
        if (n > 0) {
          read_buffer.append(buf, static_cast<std::size_t>(n));
        } else if (n == 0) {
          // EOF
          if (!read_buffer.empty()) {
            std::string line = std::move(read_buffer);
            read_buffer.clear();
            return line;
          }
          return "";
        }
      } else if (ret == 0) {
        return "";
      } else if (errno == EINTR) {
        continue;
      } else {
        return "";
      }
    }
  }

  int Wait() {
    if (in_fd >= 0) {
      ::close(in_fd);
      in_fd = -1;
    }
    if (pid > 0) {
      int status = 0;
      ::waitpid(pid, &status, 0);
      pid = -1;
      if (out_fd >= 0) {
        ::close(out_fd);
        out_fd = -1;
      }
      if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
      }
      if (WIFSIGNALED(status)) {
        std::cerr << "Process terminated by signal: " << WTERMSIG(status) << '\n';
      }
    }
    return -1;
  }
};

Process SpawnProcess(std::string const& binary_path,
                     std::vector<std::string> const& args) {
  int in_pipe[2];
  int out_pipe[2];
  CHECK(::pipe(in_pipe) == 0);
  CHECK(::pipe(out_pipe) == 0);

  pid_t pid = ::fork();
  CHECK(pid >= 0);

  if (pid == 0) {
    // Child
    ::signal(SIGPIPE, SIG_IGN);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);

    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);

    std::vector<char*> c_args;
    c_args.push_back(const_cast<char*>(binary_path.c_str()));
    for (auto const& a : args) {
      c_args.push_back(const_cast<char*>(a.c_str()));
    }
    c_args.push_back(nullptr);

    ::execv(binary_path.c_str(), c_args.data());
    std::cerr << "execv failed: " << std::strerror(errno) << '\n';
    ::_exit(127);
  }

  // Parent
  ::close(in_pipe[0]);
  ::close(out_pipe[1]);

  return Process{
      .pid = pid,
      .in_fd = in_pipe[1],
      .out_fd = out_pipe[0],
      .read_buffer = {},
  };
}

std::filesystem::path FindBinary(std::string const& argv0,
                                 std::string const& name) {
  std::filesystem::path self = argv0;
  auto dir = self.parent_path();
  auto candidate1 = dir / name;
  if (std::filesystem::exists(candidate1)) {
    return candidate1;
  }
  auto candidate2 = dir / ".." / "examples" / "chat_demo" / name;
  if (std::filesystem::exists(candidate2)) {
    return candidate2;
  }
  return name;
}

void TestTwoProcessProbePresenceAndByteDelivery(std::string const& probe_bin) {
  std::cout << "[Test 1] Real Aether Probe Two-Process Presence & Byte Delivery\n";
  auto state_dir_a = std::filesystem::temp_directory_path() / "test_aether_p2p_probe_a";
  auto state_dir_b = std::filesystem::temp_directory_path() / "test_aether_p2p_probe_b";
  std::filesystem::remove_all(state_dir_a);
  std::filesystem::remove_all(state_dir_b);

  // 1. Initial run to discover UIDs
  auto proc_a = SpawnProcess(
      probe_bin, {"--state-dir", state_dir_a.string(), "--client-name", "probe-client-a"});
  auto line_a = proc_a.ReadLine(15000);
  CHECK(line_a.rfind("READY uid=", 0) == 0);
  auto const uid_a = line_a.substr(10);
  proc_a.WriteLine("exit");
  CHECK(proc_a.Wait() == 0);

  auto proc_b = SpawnProcess(
      probe_bin, {"--state-dir", state_dir_b.string(), "--client-name", "probe-client-b"});
  auto line_b = proc_b.ReadLine(15000);
  CHECK(line_b.rfind("READY uid=", 0) == 0);
  auto const uid_b = line_b.substr(10);
  proc_b.WriteLine("exit");
  CHECK(proc_b.Wait() == 0);

  std::cout << "  Discovered UID A: " << uid_a << '\n';
  std::cout << "  Discovered UID B: " << uid_b << '\n';

  // 2. Launch both with mutual --peer-uid, heartbeat=500ms, offline=1200ms
  auto run_a = SpawnProcess(
      probe_bin,
      {"--state-dir", state_dir_a.string(), "--client-name", "probe-client-a",
       "--peer-uid", uid_b, "--heartbeat-ms", "500", "--offline-ms", "1200"});
  auto run_b = SpawnProcess(
      probe_bin,
      {"--state-dir", state_dir_b.string(), "--client-name", "probe-client-b",
       "--peer-uid", uid_a, "--heartbeat-ms", "500", "--offline-ms", "1200"});

  // Read until both report online
  bool a_online = false;
  bool b_online = false;
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

  while ((!a_online || !b_online) && std::chrono::steady_clock::now() < deadline) {
    if (!a_online) {
      auto l = run_a.ReadLine(500);
      if (l.find("PRESENCE peer=" + uid_b + " state=online") != std::string::npos) {
        a_online = true;
        std::cout << "  [Process A] peer B is Online!\n";
      }
    }
    if (!b_online) {
      auto l = run_b.ReadLine(500);
      if (l.find("PRESENCE peer=" + uid_a + " state=online") != std::string::npos) {
        b_online = true;
        std::cout << "  [Process B] peer A is Online!\n";
      }
    }
  }

  CHECK(a_online && "Process A must report B Online within 30s");
  CHECK(b_online && "Process B must report A Online within 30s");

  // 3. Application byte exchange
  run_a.WriteLine("send " + uid_b + " P2pTestFromA");
  run_b.WriteLine("send " + uid_a + " P2pTestFromB");

  bool a_rx = false;
  bool b_rx = false;
  auto const rx_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while ((!a_rx || !b_rx) && std::chrono::steady_clock::now() < rx_deadline) {
    if (!a_rx) {
      auto l = run_a.ReadLine(500);
      if (l.find("RX peer=" + uid_b) != std::string::npos &&
          l.find("P2pTestFromB") != std::string::npos) {
        a_rx = true;
        std::cout << "  [Process A] received: " << l << '\n';
      }
    }
    if (!b_rx) {
      auto l = run_b.ReadLine(500);
      if (l.find("RX peer=" + uid_a) != std::string::npos &&
          l.find("P2pTestFromA") != std::string::npos) {
        b_rx = true;
        std::cout << "  [Process B] received: " << l << '\n';
      }
    }
  }

  CHECK(a_rx && "Process A must receive application frame from B");
  CHECK(b_rx && "Process B must receive application frame from A");

  // 4. Test offline detection on timeout: terminate B, A must transition to offline
  std::cout << "  Stopping Process B to verify timeout-based Offline transition...\n";
  run_b.WriteLine("exit");
  run_b.Wait();

  bool a_offline = false;
  auto const offline_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!a_offline && std::chrono::steady_clock::now() < offline_deadline) {
    auto l = run_a.ReadLine(500);
    if (l.find("PRESENCE peer=" + uid_b + " state=offline") != std::string::npos) {
      a_offline = true;
      std::cout << "  [Process A] detected peer B Offline!\n";
    }
  }

  CHECK(a_offline && "Process A must report B Offline after heartbeat timeout");

  // 5. Test Offline -> Connecting -> Online reconnect transition
  std::cout << "  Relaunching Process B to verify Offline -> Connecting -> Online transition...\n";
  auto run_b_reconnect = SpawnProcess(
      probe_bin,
      {"--state-dir", state_dir_b.string(), "--client-name", "probe-client-b",
       "--peer-uid", uid_a, "--heartbeat-ms", "500", "--offline-ms", "1200"});

  bool a_reconnecting_seen = false;
  bool a_reonline = false;
  auto const reconnect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while ((!a_reonline) && std::chrono::steady_clock::now() < reconnect_deadline) {
    auto l = run_a.ReadLine(500);
    if (l.find("PRESENCE peer=" + uid_b + " state=connecting") != std::string::npos) {
      a_reconnecting_seen = true;
      std::cout << "  [Process A] saw reconnect transition to Connecting!\n";
    }
    if (l.find("PRESENCE peer=" + uid_b + " state=online") != std::string::npos) {
      a_reonline = true;
      std::cout << "  [Process A] saw peer B back Online!\n";
    }
  }

  CHECK(a_reonline && "Process A must see B come back Online");

  run_b_reconnect.WriteLine("exit");
  run_b_reconnect.Wait();
  run_a.WriteLine("exit");
  run_a.Wait();

  std::filesystem::remove_all(state_dir_a);
  std::filesystem::remove_all(state_dir_b);
  std::cout << "[Test 1] PASS\n";
}

// -----------------------------------------------------------------------------
// Two-Process Real Aether Chat Sync
// -----------------------------------------------------------------------------

void RunChatReplicaA(std::filesystem::path state_dir, std::string client_name,
                     std::string peer_uid) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();

  ChatAetherRuntime runtime;
  std::unique_ptr<AetherByteTransport> transport;
  ManualModelDispatcher dispatcher;
  std::mutex ready_mu;
  std::condition_variable ready_cv;
  bool ready = false;
  std::string local_uid;

  ChatAetherRuntime::Config config{
      .state_dir = state_dir,
      .client_name = client_name,
      .heartbeat_period_ms = 500,
      .offline_after_ms = 2000,
  };

  runtime.Start(
      std::move(config),
      [&local_uid, &ready_mu, &ready_cv](std::string uid) {
        {
          std::lock_guard<std::mutex> lock{ready_mu};
          local_uid = std::move(uid);
        }
        ready_cv.notify_all();
      },
      [&ready, &ready_mu, &ready_cv]() {
        {
          std::lock_guard<std::mutex> lock{ready_mu};
          ready = true;
        }
        ready_cv.notify_all();
      },
      [](std::string error) {
        std::cerr << "Aether error: " << error << '\n';
      },
      {},
      {});

  std::string my_uid;
  {
    std::unique_lock<std::mutex> lock{ready_mu};
    ready_cv.wait(lock, [&ready, &local_uid] { return ready && !local_uid.empty(); });
    my_uid = local_uid;
  }

  transport = std::make_unique<AetherByteTransport>(
      runtime, my_uid,
      [&dispatcher](std::function<void()> task) {
        dispatcher.Post(std::move(task));
      });

  // Print READY uid for parent handshake
  std::cout << "READY uid=" << my_uid << "\n" << std::flush;

  if (peer_uid.empty()) {
    std::string line;
    if (std::getline(std::cin, line)) {
      peer_uid = line;
    }
  }
  CHECK(!peer_uid.empty());

  runtime.OpenPeer(peer_uid);

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  apptraverse::SharedSyncRuntime sync{domain, storage, *transport};
  sync.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  // Setup ChatRoom
  ae::ObjId const ws_id{100};
  ae::ObjId const entry_id{101};
  ae::ObjId const link_a_id{102};
  ae::ObjId const link_b_id{103};
  ae::ObjId const room_id{104};

  auto ws = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(ws_id));
  InitializeRuntimeNode(*ws);
  BindLocalEndpoint(*ws, my_uid);

  auto entry = OpenOrSelectChat(*ws, peer_uid, [] {});
  CHECK(entry.is_valid());

  auto link_local = apptraverse::MemoryLink::ptr::Create(
      ae::CreateWith{domain}.with_id(link_a_id));
  link_local->endpoint_uid = my_uid;
  InitializeRuntimeNode(*link_local);

  auto link_remote = apptraverse::MemoryLink::ptr::Create(
      ae::CreateWith{domain}.with_id(link_b_id));
  link_remote->endpoint_uid = peer_uid;
  InitializeRuntimeNode(*link_remote);

  auto room = ChatRoom::ptr::Create(ae::CreateWith{domain}.with_id(room_id));
  InitializeRuntimeNode(*room);

  room->InstallLocalShare(link_local, apptraverse::ShareAccess::ReadWrite);
  room->InstallLocalShare(link_remote, apptraverse::ShareAccess::ReadWrite);

  BindChat(*entry, link_remote, room);

  ws.Save();
  entry.Save();
  link_local.Save();
  link_remote.Save();
  room.Save();
  for (auto& s : room->link_sync_states) {
    if (s.is_valid()) {
      s.Save();
    }
  }

  sync.RegisterNode(room);

  ae::ObjId share_to_b;
  for (auto const& share : room->shares) {
    if (share.link.is_valid() && share.link->EndpointUid() == peer_uid) {
      share_to_b = share.share_id;
      break;
    }
  }
  CHECK(share_to_b.is_valid());

  // Wait a moment for peer connection to establish, then sync initial state
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  dispatcher.Drain();
  sync.SyncInitialState(room_id, share_to_b);

  auto const sync_index = room->FindLinkSyncIndexForShare(share_to_b);
  auto state = room->link_sync_states[sync_index];

  // Wait until initial sync phase on A is Complete (meaning ACK from B for snapshot arrived)
  auto const init_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (std::chrono::steady_clock::now() < init_deadline) {
    dispatcher.Drain();
    if (state->GetInitialSyncPhase() ==
        apptraverse::InitialSyncPhase::Complete) {
      break;
    }
    if (state->GetInitialSyncPhase() ==
        apptraverse::InitialSyncPhase::Pending) {
      sync.SyncInitialState(room_id, share_to_b);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  dispatcher.Drain();
  CHECK(state->GetInitialSyncPhase() ==
        apptraverse::InitialSyncPhase::Complete);

  // Submit message from A
  SetDraft(*entry, "Hello from A");
  auto msg_id_a = SubmitDraft(*ws, *entry, 1000001);
  sync.SyncNextEvent(room_id, share_to_b);

  // Wait for B's reply to arrive (total messages in room == 2) and A's message ACKed
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(25);
  while (std::chrono::steady_clock::now() < deadline) {
    dispatcher.Drain();
    if (room->messages.size() >= 2 && !state->HasPendingEvent()) {
      break;
    }
    if (state->HasPendingEvent()) {
      sync.SyncNextEvent(room_id, share_to_b);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  dispatcher.Drain();

  CHECK(room->messages.size() == 2);
  std::cout << "CHAT_SYNC_OK messages=" << room->messages.size() << "\n"
            << std::flush;

  runtime.RequestStop();
  runtime.Join();
}

void RunChatReplicaB(std::filesystem::path state_dir, std::string client_name,
                     std::string peer_uid) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::chat_demo::EnsureChatDemoModelRegistration();

  ChatAetherRuntime runtime;
  std::unique_ptr<AetherByteTransport> transport;
  ManualModelDispatcher dispatcher;
  std::mutex ready_mu;
  std::condition_variable ready_cv;
  bool ready = false;
  std::string local_uid;

  ChatAetherRuntime::Config config{
      .state_dir = state_dir,
      .client_name = client_name,
      .heartbeat_period_ms = 500,
      .offline_after_ms = 2000,
  };

  runtime.Start(
      std::move(config),
      [&local_uid, &ready_mu, &ready_cv](std::string uid) {
        {
          std::lock_guard<std::mutex> lock{ready_mu};
          local_uid = std::move(uid);
        }
        ready_cv.notify_all();
      },
      [&ready, &ready_mu, &ready_cv]() {
        {
          std::lock_guard<std::mutex> lock{ready_mu};
          ready = true;
        }
        ready_cv.notify_all();
      },
      [](std::string error) {
        std::cerr << "Aether error: " << error << '\n';
      },
      {},
      {});

  std::string my_uid;
  {
    std::unique_lock<std::mutex> lock{ready_mu};
    ready_cv.wait(lock, [&ready, &local_uid] { return ready && !local_uid.empty(); });
    my_uid = local_uid;
  }

  transport = std::make_unique<AetherByteTransport>(
      runtime, my_uid,
      [&dispatcher](std::function<void()> task) {
        dispatcher.Post(std::move(task));
      });

  // Print READY uid for parent handshake
  std::cout << "READY uid=" << my_uid << "\n" << std::flush;

  if (peer_uid.empty()) {
    std::string line;
    if (std::getline(std::cin, line)) {
      peer_uid = line;
    }
  }
  CHECK(!peer_uid.empty());

  runtime.OpenPeer(peer_uid);

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  apptraverse::SharedSyncRuntime sync{domain, storage, *transport};
  sync.AllowStandaloneEventClass(MessageAddedEvent::kClassId);

  ae::ObjId const room_id{104};
  sync.ExpectInitialNode(room_id);

  // Wait for initial snapshot import from A
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
  ChatRoom::ptr room_b;
  while (std::chrono::steady_clock::now() < deadline) {
    dispatcher.Drain();
    auto imported = sync.FindNode(room_id);
    if (imported.is_valid()) {
      room_b = ChatRoom::ptr::MakeFromThis(static_cast<ChatRoom*>(&*imported));
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  dispatcher.Drain();
  CHECK(room_b.is_valid() && "B must import room snapshot from A");

  // Find share back to A
  ae::ObjId share_to_a_on_b;
  for (auto const& share : room_b->shares) {
    if (share.link.is_valid() && share.link->EndpointUid() == peer_uid) {
      share_to_a_on_b = share.share_id;
      break;
    }
  }
  CHECK(share_to_a_on_b.is_valid());

  // Setup local workspace on B for reply
  ae::ObjId const ws_b_id{200};
  auto ws_b = ChatWorkspace::ptr::Create(ae::CreateWith{domain}.with_id(ws_b_id));
  InitializeRuntimeNode(*ws_b);
  BindLocalEndpoint(*ws_b, my_uid);

  auto entry_b = OpenOrSelectChat(*ws_b, peer_uid, [] {});
  apptraverse::Link::ptr remote_link_on_b;
  for (auto const& share : room_b->shares) {
    if (share.share_id == share_to_a_on_b) {
      remote_link_on_b = share.link;
      break;
    }
  }
  CHECK(remote_link_on_b.is_valid());
  BindChat(*entry_b, remote_link_on_b, room_b);

  auto const sync_index_b = room_b->FindLinkSyncIndexForShare(share_to_a_on_b);
  auto state_b = room_b->link_sync_states[sync_index_b];

  // Wait until A's initial message arrives (room_b has at least 1 message)
  auto const msg_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(25);
  while (std::chrono::steady_clock::now() < msg_deadline) {
    dispatcher.Drain();
    if (!room_b->messages.empty()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  dispatcher.Drain();
  CHECK(!room_b->messages.empty() && "B must receive initial message from A");
  CHECK(room_b->messages[0].text == "Hello from A");

  // Submit reply message from B
  SetDraft(*entry_b, "Reply from B");
  auto msg_id_b = SubmitDraft(*ws_b, *entry_b, 1000002);
  sync.SyncNextEvent(room_id, share_to_a_on_b);

  // Wait until B has 2 messages and B's reply is ACKed
  while (std::chrono::steady_clock::now() < deadline) {
    dispatcher.Drain();
    if (room_b->messages.size() >= 2 && !state_b->HasPendingEvent()) {
      break;
    }
    if (state_b->HasPendingEvent()) {
      sync.SyncNextEvent(room_id, share_to_a_on_b);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  dispatcher.Drain();

  CHECK(room_b->messages.size() == 2);
  std::cout << "CHAT_SYNC_OK messages=" << room_b->messages.size() << "\n"
            << std::flush;

  runtime.RequestStop();
  runtime.Join();
}

void TestTwoProcessChatSync(std::string const& self_bin) {
  std::cout << "[Test 2] Real Aether Chat Sync Two-Process (Snapshot + ACK + 2-Way Events)\n";
  auto state_dir_a = std::filesystem::temp_directory_path() / "test_aether_chat_a";
  auto state_dir_b = std::filesystem::temp_directory_path() / "test_aether_chat_b";
  std::filesystem::remove_all(state_dir_a);
  std::filesystem::remove_all(state_dir_b);

  auto proc_a = SpawnProcess(
      self_bin, {"--replica-a", state_dir_a.string(), "chat-sync-a"});
  auto line_a = proc_a.ReadLine(15000);
  CHECK(line_a.rfind("READY uid=", 0) == 0);
  auto const uid_a = line_a.substr(10);

  auto proc_b = SpawnProcess(
      self_bin, {"--replica-b", state_dir_b.string(), "chat-sync-b"});
  auto line_b = proc_b.ReadLine(15000);
  CHECK(line_b.rfind("READY uid=", 0) == 0);
  auto const uid_b = line_b.substr(10);

  std::cout << "  Replica A UID: " << uid_a << '\n';
  std::cout << "  Replica B UID: " << uid_b << '\n';

  // Send peer UIDs to replicas
  proc_a.WriteLine(uid_b);
  proc_b.WriteLine(uid_a);

  // Both should report CHAT_SYNC_OK messages=2
  auto res_a = proc_a.ReadLine(30000);
  auto res_b = proc_b.ReadLine(30000);

  std::cout << "  Replica A result: " << res_a << '\n';
  std::cout << "  Replica B result: " << res_b << '\n';

  CHECK(res_a.find("CHAT_SYNC_OK") != std::string::npos);
  CHECK(res_b.find("CHAT_SYNC_OK") != std::string::npos);

  CHECK(proc_a.Wait() == 0);
  CHECK(proc_b.Wait() == 0);

  std::filesystem::remove_all(state_dir_a);
  std::filesystem::remove_all(state_dir_b);
  std::cout << "[Test 2] PASS\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc >= 4 && std::string_view(argv[1]) == "--replica-a") {
    std::string peer_uid = (argc >= 5) ? argv[4] : "";
    RunChatReplicaA(argv[2], argv[3], peer_uid);
    return 0;
  }
  if (argc >= 4 && std::string_view(argv[1]) == "--replica-b") {
    std::string peer_uid = (argc >= 5) ? argv[4] : "";
    RunChatReplicaB(argv[2], argv[3], peer_uid);
    return 0;
  }

  std::string const probe_bin =
      FindBinary(argv[0], "apptraverse_chat_aether_probe").string();
  CHECK(std::filesystem::exists(probe_bin) &&
        "apptraverse_chat_aether_probe must exist");

  TestTwoProcessProbePresenceAndByteDelivery(probe_bin);
  TestTwoProcessChatSync(argv[0]);

  std::cout << "All real Aether P2P and Chat sync tests passed!\n";
  return 0;
}
