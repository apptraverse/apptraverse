#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "aether_byte_transport.h"
#include "aether_frame_endpoint.h"
#include "apptraverse/byte_transport.h"
#include "chat_presence.h"

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

namespace {

using apptraverse::EndpointAvailability;
using apptraverse::example::chat_demo::AetherByteTransport;
using apptraverse::example::chat_demo::IAetherFrameEndpoint;
using apptraverse::example::chat_demo::ModelTask;
using apptraverse::example::chat_demo::PeerPresence;

class FakeFrameEndpoint : public IAetherFrameEndpoint {
 public:
  void Start(Config /*config*/, LocalUidCallback /*on_uid*/,
             ReadyCallback /*on_ready*/, FailedCallback /*on_failed*/,
             FrameCallback on_frame, PresenceCallback /*on_presence*/,
             LocalConnectivityCallback /*on_local_connectivity*/ = {},
             ControlCallback on_control = {}) override {
    callback_ = std::move(on_frame);
    control_callback_ = std::move(on_control);
  }

  void OpenPeer(std::string /*peer_uid*/) override {}
  void ClosePeer(std::string /*peer_uid*/) override {}

  void Send(std::string peer_uid, std::vector<std::uint8_t> bytes) override {
    last_sent_peer = std::move(peer_uid);
    last_sent_bytes = std::move(bytes);
  }

  void SendControl(std::string peer_uid,
                   std::vector<std::uint8_t> bytes) override {
    last_control_peer = std::move(peer_uid);
    last_control_bytes = std::move(bytes);
  }

  void SetFrameCallback(FrameCallback callback) override {
    callback_ = std::move(callback);
  }

  void RequestStop() override {}
  void Join() override {}

  void InjectFrame(std::string source_uid, std::vector<std::uint8_t> bytes) {
    if (callback_) {
      callback_(std::move(source_uid), std::move(bytes));
    }
  }

  std::string last_sent_peer;
  std::vector<std::uint8_t> last_sent_bytes;
  std::string last_control_peer;
  std::vector<std::uint8_t> last_control_bytes;
  FrameCallback callback_;
  ControlCallback control_callback_;
};

class TestDispatcher {
 public:
  void Post(ModelTask task) {
    queue_.push_back(std::move(task));
  }

  void Drain() {
    std::vector<ModelTask> to_run;
    to_run.swap(queue_);
    for (auto& task : to_run) {
      if (task) {
        task();
      }
    }
  }

  std::size_t pending_count() const {
    return queue_.size();
  }

 private:
  std::vector<ModelTask> queue_;
};

struct ReceiverState {
  int call_count{0};
  std::string last_source;
  std::vector<std::uint8_t> last_bytes;

  static void ReceiveThunk(void* ctx, std::string const& source,
                           std::vector<std::uint8_t> const& bytes) {
    auto* self = static_cast<ReceiverState*>(ctx);
    self->call_count++;
    self->last_source = source;
    self->last_bytes = bytes;
  }
};

struct AvailabilityState {
  int call_count{0};
  std::string last_endpoint;
  EndpointAvailability last_availability{EndpointAvailability::Unknown};

  static void AvailabilityThunk(void* ctx, std::string const& endpoint,
                                EndpointAvailability availability) {
    auto* self = static_cast<AvailabilityState*>(ctx);
    self->call_count++;
    self->last_endpoint = endpoint;
    self->last_availability = availability;
  }
};

}  // namespace

int main() {
  FakeFrameEndpoint endpoint;
  TestDispatcher dispatcher;
  ReceiverState receiver;

  auto transport = std::make_unique<AetherByteTransport>(
      endpoint, "local-123",
      [&dispatcher](ModelTask task) {
        dispatcher.Post(std::move(task));
      });

  // 1. Bind ReceiveFn.
  transport->BindReceive(&receiver, &ReceiverState::ReceiveThunk);

  // 2. FakeEndpoint.InjectFrame on the test's "Aether side".
  std::vector<std::uint8_t> const frame1 = {'H', 'e', 'l', 'l', 'o'};
  endpoint.InjectFrame("peer-abc", frame1);

  // 3. Verify ReceiveFn has NOT run yet.
  CHECK(receiver.call_count == 0);
  CHECK(dispatcher.pending_count() == 1);

  // 4. dispatcher.Drain().
  dispatcher.Drain();

  // 5. Verify ReceiveFn ran exactly once and got exact source+bytes.
  CHECK(receiver.call_count == 1);
  CHECK(receiver.last_source == "peer-abc");
  CHECK(receiver.last_bytes == frame1);

  // 6. Inject second frame.
  std::vector<std::uint8_t> const frame2 = {'W', 'o', 'r', 'l', 'd'};
  endpoint.InjectFrame("peer-xyz", frame2);
  CHECK(receiver.call_count == 1);
  CHECK(dispatcher.pending_count() == 1);

  // 7. Destroy AetherByteTransport BEFORE Drain.
  transport.reset();

  // 8. Drain.
  dispatcher.Drain();

  // 9. Verify no callback after destruction.
  CHECK(receiver.call_count == 1);

  // 10. Verify ClearReceive before Drain also suppresses queued delivery.
  auto transport2 = std::make_unique<AetherByteTransport>(
      endpoint, "local-456",
      [&dispatcher](ModelTask task) {
        dispatcher.Post(std::move(task));
      });
  transport2->BindReceive(&receiver, &ReceiverState::ReceiveThunk);

  std::vector<std::uint8_t> const frame3 = {'T', 'e', 's', 't'};
  endpoint.InjectFrame("peer-def", frame3);
  CHECK(dispatcher.pending_count() == 1);

  // ClearReceive before drain
  transport2->ClearReceive();
  dispatcher.Drain();

  // Must not have called receiver
  CHECK(receiver.call_count == 1);

  // 11. Presence → Availability; repeated Online does not re-notify.
  AvailabilityState availability;
  transport2->BindAvailability(&availability,
                               &AvailabilityState::AvailabilityThunk);
  CHECK(transport2->Availability("peer-1") == EndpointAvailability::Unknown);
  transport2->NotePeerPresence("peer-1", PeerPresence::kOffline);
  CHECK(availability.call_count == 1);
  CHECK(availability.last_availability == EndpointAvailability::Offline);
  CHECK(transport2->Availability("peer-1") == EndpointAvailability::Offline);
  transport2->NotePeerPresence("peer-1", PeerPresence::kOnline);
  CHECK(availability.call_count == 2);
  CHECK(availability.last_availability == EndpointAvailability::Online);
  transport2->NotePeerPresence("peer-1", PeerPresence::kOnline);
  CHECK(availability.call_count == 2);

  // 12. Stale availability after destroy must not notify.
  transport2->NotePeerPresence("peer-2", PeerPresence::kOffline);
  CHECK(availability.call_count == 3);
  transport2.reset();
  // No transport to call; binding was cleared on destroy.

  std::cout << "aether_byte_transport_dispatch_test passed!\n";
  return 0;
}
