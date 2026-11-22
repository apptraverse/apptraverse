#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include "apptraverse/chat_session.h"
using namespace apptraverse;
using namespace std::chrono_literals;
#define CHECK(x)                                            \
  do {                                                      \
    if (!(x)) {                                             \
      std::cerr << "FAIL " #x " line " << __LINE__ << '\n'; \
      std::abort();                                         \
    }                                                       \
  } while (false)
namespace {
struct Hub {
  struct Packet {
    std::string source;
    std::vector<std::uint8_t> bytes;
  };
  std::mutex mutex;
  std::map<std::string, std::deque<Packet>> inbox;
  std::atomic<bool> busy_during_receive{};
};
class Transport final : public IConnectionTransport {
  std::string id_;
  Hub* hub_{};
  Receiver receiver_;
  bool receiving_{};

 public:
  explicit Transport(Hub* hub = nullptr, std::string id = "ram-chat-endpoint")
      : id_{std::move(id)}, hub_{hub} {
    if (hub_) {
      std::lock_guard lock{hub_->mutex};
      hub_->inbox[id_] = {};
    }
  }
  ~Transport() {
    if (hub_) {
      std::lock_guard lock{hub_->mutex};
      hub_->inbox.erase(id_);
    }
  }
  void SetReceiver(Receiver receiver) override {
    receiver_ = std::move(receiver);
  }
  bool Send(std::string const& destination,
            std::vector<std::uint8_t> const& bytes) override {
    if (!hub_) return true;
    if (receiving_ && hub_->busy_during_receive) return false;
    std::lock_guard lock{hub_->mutex};
    auto target = hub_->inbox.find(destination);
    if (target == hub_->inbox.end()) return false;
    target->second.push_back({id_, bytes});
    return true;
  }
  LinkAvailability Availability(std::string const&) const override {
    return LinkAvailability::Unknown;
  }
  void Service(ConnectionTime) override {
    if (!hub_) return;
    std::deque<Hub::Packet> packets;
    {
      std::lock_guard lock{hub_->mutex};
      packets.swap(hub_->inbox[id_]);
    }
    receiving_ = true;
    for (auto& packet : packets)
      if (receiver_) receiver_(packet.source, packet.bytes);
    receiving_ = false;
  }
  void NetworkChanged(NetworkObservation) override {}
  bool Ready() const override { return true; }
  TransportPhase Phase() const override { return TransportPhase::Connected; }
  std::string const& EndpointId() const override { return id_; }
  std::string Name() const override { return "inert-memory"; }
  bool WatchPeer(std::string const&) override { return true; }
};
class Presence final : public IPresenceSource {
 public:
  void Service(ConnectionTime) override {}
  void NetworkChanged(NetworkObservation) override {}
  void Watch(PresenceTarget) override {}
  void Unwatch(PresenceTarget const&) override {}
  std::vector<PresenceReading> Observations() const override { return {}; }
};
template <class F>
void Until(F ready) {
  auto limit = std::chrono::steady_clock::now() + 5s;
  while (!ready()) {
    CHECK(std::chrono::steady_clock::now() < limit);
    std::this_thread::sleep_for(2ms);
  }
}
}  // namespace
int main() {
  auto path =
      std::filesystem::temp_directory_path() /
      ("apptraverse-chat-ram-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(path);
  // An old experimental business file is neither read nor rewritten.
  {
    std::ofstream file{path / "user.bin"};
    file << "old-business-sentinel";
  }
  std::atomic<unsigned> sdk_flushes{};
  chat::ChatSession session{path,
                            "RAM tester",
                            {},
                            {},
                            [&] {
                              ++sdk_flushes;
                              return true;
                            },
                            [](auto const&) {
                              return ConnectionAdapters{
                                  std::make_unique<Transport>(),
                                  std::make_unique<Presence>()};
                            }};
  session.Start();
  Until([&] { return session.View().ready; });
  CHECK(!session.View().in_room);
  session.CreateRoom();
  Until([&] { return session.View().in_room; });
  std::promise<bool> sent;
  auto accepted = sent.get_future();
  session.Send("private RAM message", [&](bool ok) { sent.set_value(ok); });
  CHECK(accepted.wait_for(5s) == std::future_status::ready && accepted.get());
  Until([&] { return session.View().messages.size() == 1; });
  std::promise<bool> checkpoint;
  auto completed = checkpoint.get_future();
  session.Checkpoint([&](bool ok) { checkpoint.set_value(ok); });
  CHECK(completed.wait_for(5s) == std::future_status::ready && completed.get());
  CHECK(sdk_flushes == 1);
  session.Stop();
  session.Start();
  Until([&] { return session.View().ready && !session.View().in_room; });
  CHECK(session.View().messages.empty() &&
        session.View().message_records.empty() &&
        session.View().pending_packets == 0);
  session.Stop();
  CHECK(sdk_flushes == 2);
  std::ifstream file{path / "user.bin"};
  std::string sentinel;
  std::getline(file, sentinel);
  CHECK(sentinel == "old-business-sentinel");
  file.close();
  for (auto const& entry : std::filesystem::directory_iterator(path))
    CHECK(entry.path().filename() == "user.bin");
  std::filesystem::remove_all(path);
  {
    Hub hub;
    std::string host_id = "a1111111-1111-4111-8111-111111111111",
                peer_id = "b2222222-2222-4222-8222-222222222222";
    auto factory = [&](std::string id) {
      return [&, id](auto const&) {
        return ConnectionAdapters{std::make_unique<Transport>(&hub, id),
                                  std::make_unique<Presence>()};
      };
    };
    chat::ChatSession host{path / "host", "Host", {}, {}, {}, factory(host_id)};
    chat::ChatSession peer{path / "peer", "Peer", {}, {}, {}, factory(peer_id)};
    host.Start();
    peer.Start();
    Until([&] { return host.View().ready && peer.View().ready; });
    host.CreateRoom();
    Until([&] { return host.View().in_room; });
    peer.JoinRoom(host_id);
    Until([&] {
      return peer.View().in_room && host.View().participants.size() == 2;
    });
    std::promise<bool> local_publication;
    auto local_visible = local_publication.get_future();
    peer.Send("before restart", [&](bool accepted) {
      auto const local = peer.View();
      local_publication.set_value(accepted && local.messages.size() == 1 &&
          local.messages.back() == "Peer: before restart" &&
          local.message_records.size() == 1 && !local.message_records.back().delivered);
    });
    CHECK(local_visible.wait_for(5s) == std::future_status::ready && local_visible.get());
    Until([&] {
      return host.View().messages.size() == 1 &&
             peer.View().messages.size() == 1;
    });
    auto first = peer.View().message_records.front().id;
    peer.Stop();
    // An outstanding write can occupy the SDK slot until Service finishes
    // dispatching inbound traffic for the returning participant.
    hub.busy_during_receive = true;
    peer.Start();
    Until([&] { return peer.View().ready && !peer.View().in_room; });
    CHECK(peer.View().messages.empty());
    peer.JoinRoom(host_id);
    Until([&] {
      return peer.View().in_room && peer.View().messages.size() == 1;
    });
    CHECK(peer.View().participants.size() == 2 &&
          host.View().participants.size() == 2);
    peer.Send("after restart");
    Until([&] {
      return host.View().messages.size() == 2 &&
             peer.View().messages.size() == 2;
    });
    CHECK(peer.View().message_records.back().id != first);
    host.Send("after rejoin host reply");
    Until([&] { return peer.View().messages.size() == 3; });
    CHECK(peer.View().messages.back() == "Host: after rejoin host reply");
    peer.Stop();
    host.Stop();
    CHECK(!std::filesystem::exists(path / "host/user.bin") &&
          !std::filesystem::exists(path / "peer/user.bin"));
  }
  std::filesystem::remove_all(path);
  std::cout << "PASS chat RAM commit/checkpoint, same-identity "
               "restart/rejoin/send and ignored business disk file\n";
}
