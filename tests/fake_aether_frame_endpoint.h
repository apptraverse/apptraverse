#ifndef APPTRAVERSE_TESTS_FAKE_AETHER_FRAME_ENDPOINT_H_
#define APPTRAVERSE_TESTS_FAKE_AETHER_FRAME_ENDPOINT_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether_frame_endpoint.h"
#include "apptraverse/memory_transport.h"

namespace apptraverse::example::chat_demo::test {

// Single-threaded MemoryNetwork driver. Session workers only enqueue sends;
// this coordinator owns MemoryTransport::Send / DeliverNext.
class FakeEndpointCoordinator {
 public:
  explicit FakeEndpointCoordinator(apptraverse::MemoryNetwork& network)
      : network_(network) {}

  ~FakeEndpointCoordinator() {
    RequestStop();
    Join();
  }

  void Start() {
    std::lock_guard<std::mutex> lock{mu_};
    if (running_) {
      return;
    }
    stop_ = false;
    running_ = true;
    thread_ = std::thread([this] { Run(); });
  }

  void RequestStop() {
    {
      std::lock_guard<std::mutex> lock{mu_};
      stop_ = true;
    }
    cv_.notify_all();
  }

  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
    running_ = false;
  }

  apptraverse::MemoryTransport& EnsureTransport(
      std::string const& endpoint_uid,
      IAetherFrameEndpoint::FrameCallback on_frame) {
    std::lock_guard<std::mutex> lock{mu_};
    auto it = endpoints_.find(endpoint_uid);
    if (it != endpoints_.end()) {
      it->second.on_frame = std::move(on_frame);
      return *it->second.transport;
    }
    EndpointSlot slot;
    slot.transport =
        std::make_unique<apptraverse::MemoryTransport>(network_, endpoint_uid);
    slot.on_frame = std::move(on_frame);
    auto* raw = slot.transport.get();
    raw->BindReceive(this, &FakeEndpointCoordinator::ReceiveThunk);
    endpoints_.emplace(endpoint_uid, std::move(slot));
    return *raw;
  }

  void DropTransport(std::string const& endpoint_uid) {
    std::lock_guard<std::mutex> lock{mu_};
    endpoints_.erase(endpoint_uid);
  }

  void EnqueueSend(std::string from, std::string to,
                   std::vector<std::uint8_t> bytes) {
    {
      std::lock_guard<std::mutex> lock{mu_};
      sends_.push_back(
          PendingSend{std::move(from), std::move(to), std::move(bytes)});
    }
    cv_.notify_all();
  }

 private:
  struct PendingSend {
    std::string from;
    std::string to;
    std::vector<std::uint8_t> bytes;
  };

  struct EndpointSlot {
    std::unique_ptr<apptraverse::MemoryTransport> transport;
    IAetherFrameEndpoint::FrameCallback on_frame;
  };

  static void ReceiveThunk(void* ctx, std::string const& source,
                           std::vector<std::uint8_t> const& bytes) {
    auto* self = static_cast<FakeEndpointCoordinator*>(ctx);
    self->OnDelivered(source, bytes);
  }

  void OnDelivered(std::string const& source,
                   std::vector<std::uint8_t> const& bytes) {
    IAetherFrameEndpoint::FrameCallback callback;
    {
      std::lock_guard<std::mutex> lock{mu_};
      // Destination is the currently delivering transport's local uid, tracked
      // via delivering_to_ set around DeliverNext.
      auto it = endpoints_.find(delivering_to_);
      if (it != endpoints_.end()) {
        callback = it->second.on_frame;
      }
    }
    if (callback) {
      callback(source, bytes);
    }
  }

  void Run() {
    for (;;) {
      PendingSend send;
      {
        std::unique_lock<std::mutex> lock{mu_};
        cv_.wait(lock, [&] { return stop_ || !sends_.empty(); });
        if (stop_ && sends_.empty()) {
          break;
        }
        send = std::move(sends_.front());
        sends_.pop_front();
      }

      apptraverse::MemoryTransport* transport = nullptr;
      {
        std::lock_guard<std::mutex> lock{mu_};
        auto it = endpoints_.find(send.from);
        if (it != endpoints_.end()) {
          transport = it->second.transport.get();
        }
      }
      if (transport == nullptr) {
        continue;
      }
      transport->Send(send.to, std::move(send.bytes));
      {
        std::lock_guard<std::mutex> lock{mu_};
        delivering_to_ = send.to;
      }
      while (network_.DeliverNext(send.from, send.to)) {
      }
      {
        std::lock_guard<std::mutex> lock{mu_};
        delivering_to_.clear();
      }
    }
  }

  apptraverse::MemoryNetwork& network_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<PendingSend> sends_;
  std::unordered_map<std::string, EndpointSlot> endpoints_;
  std::string delivering_to_;
  std::thread thread_;
  bool stop_{false};
  bool running_{false};
};

// Fake endpoint: opaque bytes + scripted UID/ready/presence. No Domain/Node.
class FakeAetherFrameEndpoint : public IAetherFrameEndpoint {
 public:
  FakeAetherFrameEndpoint(FakeEndpointCoordinator& coordinator,
                          std::string local_uid)
      : coordinator_(coordinator), local_uid_(std::move(local_uid)) {}

  ~FakeAetherFrameEndpoint() override {
    RequestStop();
    Join();
  }

  void Start(Config /*config*/, LocalUidCallback on_uid,
             ReadyCallback on_ready, FailedCallback /*on_failed*/,
             FrameCallback on_frame,
             PresenceCallback on_presence) override {
    RequestStop();
    Join();
    {
      std::lock_guard<std::mutex> lock{mu_};
      on_uid_ = std::move(on_uid);
      on_ready_ = std::move(on_ready);
      on_frame_ = std::move(on_frame);
      on_presence_ = std::move(on_presence);
      stop_ = false;
      ready_signaled_ = !defer_ready_;
    }
    coordinator_.EnsureTransport(
        local_uid_, [this](std::string source, std::vector<std::uint8_t> bytes) {
          FrameCallback callback;
          {
            std::lock_guard<std::mutex> lock{mu_};
            callback = on_frame_;
          }
          if (callback) {
            callback(std::move(source), std::move(bytes));
          }
        });
    worker_ = std::thread([this] {
      LocalUidCallback on_uid;
      {
        std::lock_guard<std::mutex> lock{mu_};
        on_uid = on_uid_;
      }
      if (on_uid) {
        on_uid(local_uid_);
      }
      {
        std::unique_lock<std::mutex> lock{mu_};
        ready_cv_.wait(lock, [&] { return stop_ || ready_signaled_; });
        if (stop_) {
          return;
        }
      }
      ReadyCallback ready;
      {
        std::lock_guard<std::mutex> lock{mu_};
        ready = on_ready_;
      }
      if (ready) {
        ready();
      }
    });
  }

  void SetDeferReady(bool defer) { defer_ready_ = defer; }

  void SignalReady() {
    {
      std::lock_guard<std::mutex> lock{mu_};
      ready_signaled_ = true;
    }
    ready_cv_.notify_all();
  }

  void InjectPresence(std::string peer_uid, PeerPresence presence) {
    PresenceCallback callback;
    {
      std::lock_guard<std::mutex> lock{mu_};
      callback = on_presence_;
    }
    if (callback) {
      callback(std::move(peer_uid), presence);
    }
  }

  void InjectFrame(std::string source_uid, std::vector<std::uint8_t> bytes) {
    FrameCallback callback;
    {
      std::lock_guard<std::mutex> lock{mu_};
      callback = on_frame_;
    }
    if (callback) {
      callback(std::move(source_uid), std::move(bytes));
    }
  }

  void OpenPeer(std::string peer_uid) override {
    std::lock_guard<std::mutex> lock{mu_};
    opened_peers_.push_back(std::move(peer_uid));
  }

  void ClosePeer(std::string peer_uid) override {
    std::lock_guard<std::mutex> lock{mu_};
    closed_peers_.push_back(std::move(peer_uid));
  }

  void Send(std::string peer_uid, std::vector<std::uint8_t> bytes) override {
    coordinator_.EnqueueSend(local_uid_, std::move(peer_uid), std::move(bytes));
  }

  void SetFrameCallback(FrameCallback callback) override {
    std::lock_guard<std::mutex> lock{mu_};
    on_frame_ = std::move(callback);
    coordinator_.EnsureTransport(local_uid_, on_frame_);
  }

  void RequestStop() override {
    {
      std::lock_guard<std::mutex> lock{mu_};
      stop_ = true;
      ready_signaled_ = true;
    }
    ready_cv_.notify_all();
    coordinator_.DropTransport(local_uid_);
  }

  void Join() override {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  std::string const& local_uid() const { return local_uid_; }

 private:
  FakeEndpointCoordinator& coordinator_;
  std::string local_uid_;
  bool defer_ready_{false};

  mutable std::mutex mu_;
  std::condition_variable ready_cv_;
  bool stop_{false};
  bool ready_signaled_{false};
  LocalUidCallback on_uid_;
  ReadyCallback on_ready_;
  FrameCallback on_frame_;
  PresenceCallback on_presence_;
  std::thread worker_;
  std::vector<std::string> opened_peers_;
  std::vector<std::string> closed_peers_;
};

}  // namespace apptraverse::example::chat_demo::test

#endif  // APPTRAVERSE_TESTS_FAKE_AETHER_FRAME_ENDPOINT_H_
