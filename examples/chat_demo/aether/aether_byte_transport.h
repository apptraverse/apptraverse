#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_BYTE_TRANSPORT_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_BYTE_TRANSPORT_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "apptraverse/byte_transport.h"
#include "aether_frame_endpoint.h"
#include "chat_presence.h"

namespace apptraverse::example::chat_demo {

using ModelTask = std::function<void()>;
using ModelDispatch = std::function<void(ModelTask)>;

class AetherByteTransport final : public apptraverse::IByteTransport {
 public:
  struct ReceiveBinding {
    std::mutex mu;
    bool active{true};
    void* receive_ctx{nullptr};
    ReceiveFn receive_fn{nullptr};
    void* availability_ctx{nullptr};
    AvailabilityFn availability_fn{nullptr};
    ModelDispatch dispatch;
  };

  AetherByteTransport(IAetherFrameEndpoint& endpoint,
                      std::string local_endpoint_uid,
                      ModelDispatch dispatch_to_model);
  ~AetherByteTransport() override;

  AetherByteTransport(AetherByteTransport const&) = delete;
  AetherByteTransport& operator=(AetherByteTransport const&) = delete;

  std::string const& local_endpoint_uid() const override {
    return local_endpoint_uid_;
  }

  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override;

  void BindReceive(void* ctx, ReceiveFn fn) override;
  void ClearReceive() override;

  EndpointAvailability Availability(
      std::string const& endpoint) const override;
  void BindAvailability(void* ctx, AvailabilityFn fn) override;
  void ClearAvailability() override;

  // Model-thread only. Maps PeerPresence into EndpointAvailability for the
  // SharedSyncRuntime scheduler. Repeated Online with no change does not
  // notify the availability callback.
  void NotePeerPresence(std::string const& peer_uid, PeerPresence presence);

 private:
  static EndpointAvailability FromPresence(PeerPresence presence);

  IAetherFrameEndpoint& endpoint_;
  std::string local_endpoint_uid_;
  std::shared_ptr<ReceiveBinding> receive_binding_;
  // Observed only on the model thread that owns this transport.
  std::map<std::string, EndpointAvailability> availability_;
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_BYTE_TRANSPORT_H_
