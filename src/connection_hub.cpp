#include "apptraverse/connection_hub.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

#include "aether-miscpp/serialization/binary_archive.h"
namespace apptraverse {
namespace {
struct Frame {
  std::string channel;
  std::vector<std::uint8_t> payload;
  AE_REFLECT_MEMBERS(channel, payload)
};
bool Encode(Frame const& frame, std::vector<std::uint8_t>& bytes) {
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{buffer};
  return archive.Save(frame).IsOk();
}
bool Decode(std::vector<std::uint8_t> const& bytes, Frame& frame) {
  auto copy = bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{buffer};
  return archive.Load(frame).IsOk() &&
         archive.buffer().read_position() == copy.size() &&
         !frame.channel.empty() && frame.channel.size() <= 1024;
}
struct Packet {
  std::string peer;
  std::vector<std::uint8_t> bytes;
  std::string identity;
  bool transient{false};
};
struct Channel {
  std::string id;
  unsigned leases{2};
  std::vector<std::string> peers;
  std::vector<PresenceTarget> targets;
  std::deque<Packet> incoming, outgoing;
  std::map<std::pair<std::string, std::string>, bool> forgotten;
};
struct Shared {
  std::mutex mutex;
  std::map<std::string, std::weak_ptr<Channel>> channels;
  NetworkObservation network;
  SessionObservation observation;
  std::vector<PresenceReading> readings;
  std::map<std::string, IConnectionTransport::Path> paths;
  bool ready{false};
};
void ReleaseChannel(std::shared_ptr<Shared> const& shared,
                    std::shared_ptr<Channel> const& channel) {
  std::lock_guard lock{shared->mutex};
  if (--channel->leases == 0) {
    channel->incoming.clear();
    channel->outgoing.clear();
    auto it = shared->channels.find(channel->id);
    if (it != shared->channels.end() && it->second.lock() == channel)
      shared->channels.erase(it);
  }
}
class ChannelTransport final : public IConnectionTransport {
  std::shared_ptr<Shared> shared_;
  std::shared_ptr<Channel> channel_;
  Receiver receiver_;
  SessionObservation observation_;
  std::map<std::string, Path> paths_;
  bool ready_{false};

 public:
  ChannelTransport(std::shared_ptr<Shared> s, std::shared_ptr<Channel> c)
      : shared_{std::move(s)}, channel_{std::move(c)} {}
  ~ChannelTransport() override { ReleaseChannel(shared_, channel_); }
  void Service(ConnectionTime) override {
    std::deque<Packet> received;
    {
      std::lock_guard lock{shared_->mutex};
      received.swap(channel_->incoming);
      observation_ = shared_->observation;
      paths_ = shared_->paths;
      ready_ = shared_->ready;
    }
    for (auto const& packet : received)
      if (receiver_) receiver_(packet.peer, packet.bytes);
  }
  void NetworkChanged(NetworkObservation n) override {
    std::lock_guard lock{shared_->mutex};
    shared_->network = n;
  }
  bool Ready() const override { return ready_; }
  TransportPhase Phase() const override { return observation_.phase; }
  std::string const& EndpointId() const override {
    return observation_.local_endpoint;
  }
  std::string Name() const override { return observation_.transport; }
  bool WatchPeer(std::string const& id) override {
    if (id.empty()) return false;
    std::lock_guard lock{shared_->mutex};
    if (std::find(channel_->peers.begin(), channel_->peers.end(), id) ==
        channel_->peers.end())
      channel_->peers.push_back(id);
    return true;
  }
  Path PeerPath(std::string const& id) const override {
    auto it = paths_.find(id);
    return it == paths_.end() ? Path{} : it->second;
  }
  void SetReceiver(Receiver r) override { receiver_ = std::move(r); }
  bool Send(std::string const& peer,
            std::vector<std::uint8_t> const& bytes) override {
    if (bytes.size() > 1024 * 1024 - 4096) return false;
    std::lock_guard lock{shared_->mutex};
    if (channel_->outgoing.size() >= 64) return false;
    channel_->outgoing.push_back({peer, bytes});
    return true;
  }
  bool SendIdentified(std::string const& peer, std::string const& identity,
                      std::vector<std::uint8_t> const& bytes) override {
    if (bytes.size() > 1024 * 1024 - 4096) return false;
    std::lock_guard lock{shared_->mutex};
    if (channel_->outgoing.size() >= 64) return false;
    channel_->outgoing.push_back({peer, bytes, identity});
    return true;
  }
  void ForgetIdentified(std::string const& peer, std::string const& identity) override {
    std::lock_guard lock{shared_->mutex};
    std::erase_if(channel_->outgoing, [&](auto const& p) {
      return p.peer == peer && p.identity == identity;
    });
    channel_->forgotten[{peer, identity}] = true;
  }
  bool SendTransient(std::string const& peer,
                     std::vector<std::uint8_t> const& bytes) override {
    if (bytes.size() > 1024 * 1024 - 4096) return false;
    std::lock_guard lock{shared_->mutex};
    if (channel_->outgoing.size() >= 64) return false;
    channel_->outgoing.push_back({peer, bytes, {}, true});
    return true;
  }
  LinkAvailability Availability(std::string const&) const override {
    return LinkAvailability::Unknown;
  }
};
class ChannelPresence final : public IPresenceSource {
  std::shared_ptr<Shared> shared_;
  std::shared_ptr<Channel> channel_;
  std::vector<PresenceReading> readings_;

 public:
  ChannelPresence(std::shared_ptr<Shared> s, std::shared_ptr<Channel> c)
      : shared_{std::move(s)}, channel_{std::move(c)} {}
  ~ChannelPresence() override { ReleaseChannel(shared_, channel_); }
  void Service(ConnectionTime) override {
    std::lock_guard lock{shared_->mutex};
    readings_.clear();
    for (auto const& r : shared_->readings)
      if (std::find(channel_->targets.begin(), channel_->targets.end(),
                    r.value.target) != channel_->targets.end())
        readings_.push_back(r);
  }
  void NetworkChanged(NetworkObservation n) override {
    std::lock_guard lock{shared_->mutex};
    shared_->network = n;
  }
  void Watch(PresenceTarget t) override {
    std::lock_guard lock{shared_->mutex};
    if (std::find(channel_->targets.begin(), channel_->targets.end(), t) ==
        channel_->targets.end())
      channel_->targets.push_back(std::move(t));
  }
  void Unwatch(PresenceTarget const& t) override {
    std::lock_guard lock{shared_->mutex};
    std::erase(channel_->targets, t);
  }
  std::vector<PresenceReading> Observations() const override {
    return readings_;
  }
};
}  // namespace
struct ConnectionHub::Impl {
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();
  std::unique_ptr<ConnectionRuntime> manual;
  std::thread worker;
  std::atomic<bool> stop{false};
  std::mutex wake_mutex;
  std::condition_variable wake;
  std::vector<PresenceTarget> watched;
  bool bound{false};
  std::string last_sent_channel;
  void Service(ConnectionRuntime& runtime, ConnectionTime time) {
    NetworkObservation network;
    std::vector<std::shared_ptr<Channel>> channels;
    std::vector<PresenceTarget> targets;
    std::vector<std::string> peers;
    {
      std::lock_guard lock{shared->mutex};
      network = shared->network;
      std::erase_if(shared->channels, [&](auto const& entry) {
        auto c = entry.second.lock();
        if (!c) return true;
        channels.push_back(c);
        for (auto const& t : c->targets)
          if (std::find(targets.begin(), targets.end(), t) == targets.end())
            targets.push_back(t);
        for (auto const& p : c->peers)
          if (std::find(peers.begin(), peers.end(), p) == peers.end())
            peers.push_back(p);
        return false;
      });
    }
    runtime.NetworkChanged(network);
    for (auto const& target : targets)
      if (std::find(watched.begin(), watched.end(), target) == watched.end())
        runtime.WatchPresence(target);
    for (auto const& target : watched)
      if (std::find(targets.begin(), targets.end(), target) == targets.end())
        runtime.UnwatchPresence(target);
    watched = std::move(targets);
    runtime.Service(time);
    auto& transport = runtime.Transport();
    if (transport.Ready()) {
      if (!bound) {
        transport.SetReceiver(
            [state = shared](auto const& peer, auto const& bytes) {
              Frame frame;
              if (!Decode(bytes, frame)) return;
              std::lock_guard lock{state->mutex};
              auto it = state->channels.find(frame.channel);
              if (it == state->channels.end()) return;
              if (auto channel = it->second.lock();
                  channel && channel->incoming.size() < 64)
                channel->incoming.push_back({peer, std::move(frame.payload)});
              // An unopened/overloaded graph does not ACK. SharedSync retries
              // durably.
            });
        bound = true;
      }
      for (auto const& peer : peers) transport.WatchPeer(peer);
      auto next = std::find_if(
          channels.begin(), channels.end(),
          [&](auto const& c) { return c->id > last_sent_channel; });
      if (next != channels.end())
        std::rotate(channels.begin(), next, channels.end());
      for (auto const& channel : channels) {
        decltype(channel->forgotten) forgotten;
        { std::lock_guard lock{shared->mutex}; forgotten.swap(channel->forgotten); }
        auto qualify = [&](std::string const& id) {
          return std::to_string(channel->id.size()) + ":" + channel->id + ":" + id;
        };
        for (auto const& [key, unused] : forgotten)
          transport.ForgetIdentified(key.first, qualify(key.second));
        for (int count = 0; count < 8; ++count) {
          Packet packet;
          {
            std::lock_guard lock{shared->mutex};
            if (!channel->leases || channel->outgoing.empty()) break;
            packet = channel->outgoing.front();
          }
          std::vector<std::uint8_t> bytes;
          if (!Encode({channel->id, packet.bytes}, bytes) ||
              !(packet.transient ? transport.SendTransient(packet.peer, bytes)
                : packet.identity.empty() ? transport.Send(packet.peer, bytes)
                : transport.SendIdentified(packet.peer, qualify(packet.identity), bytes)))
            break;
          last_sent_channel = channel->id;
          {
            std::lock_guard lock{shared->mutex};
            if (channel->leases && !channel->outgoing.empty())
              channel->outgoing.pop_front();
          }
        }
      }
    }
    auto observation = runtime.Observation();
    auto readings = runtime.PresenceReadings();
    std::map<std::string, IConnectionTransport::Path> paths;
    if (transport.Ready())
      for (auto const& peer : peers)
        paths.emplace(peer, transport.PeerPath(peer));
    {
      std::lock_guard lock{shared->mutex};
      shared->observation = std::move(observation);
      shared->readings = std::move(readings);
      shared->paths = std::move(paths);
      shared->ready = transport.Ready();
    }
  }
};
ConnectionHub::ConnectionHub(Factory factory, bool run_worker)
    : impl_{std::make_unique<Impl>()} {
  if (!run_worker) {
    impl_->manual = std::make_unique<ConnectionRuntime>(factory());
    return;
  }
  impl_->worker =
      std::thread([this, factory = std::move(factory)] {
        ConnectionRuntime runtime{factory()};
        while (!impl_->stop.load()) {
          impl_->Service(runtime, ReadConnectionTime());
          std::unique_lock lock{impl_->wake_mutex};
          impl_->wake.wait_for(lock, std::chrono::milliseconds{10},
                               [this] { return impl_->stop.load(); });
        }
        if (impl_->bound) runtime.Transport().SetReceiver({});
        std::lock_guard lock{impl_->shared->mutex};
        impl_->shared->observation.phase = TransportPhase::Stopped;
        impl_->shared->ready = false;
        impl_->shared->readings.clear();
      });
}
ConnectionHub::~ConnectionHub() {
  impl_->stop.store(true);
  impl_->wake.notify_all();
  if (impl_->worker.joinable()) impl_->worker.join();
  if (impl_->manual && impl_->bound) impl_->manual->Transport().SetReceiver({});
}
void ConnectionHub::Service(ConnectionTime time) {
  if (!impl_->manual) std::abort();
  impl_->Service(*impl_->manual, time);
}
ConnectionAdapters ConnectionHub::OpenChannel(std::string id) {
  if (id.empty() || id.size() > 1024) std::abort();
  auto channel = std::make_shared<Channel>();
  channel->id = std::move(id);
  {
    std::lock_guard lock{impl_->shared->mutex};
    auto& entry = impl_->shared->channels[channel->id];
    if (!entry.expired()) std::abort();
    entry = channel;
  }
  return {std::make_unique<ChannelTransport>(impl_->shared, channel),
          std::make_unique<ChannelPresence>(impl_->shared, channel)};
}
}  // namespace apptraverse
