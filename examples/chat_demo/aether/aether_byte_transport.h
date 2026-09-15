#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_BYTE_TRANSPORT_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_BYTE_TRANSPORT_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "apptraverse/byte_transport.h"
#include "chat_aether_runtime.h"

namespace apptraverse::example::chat_demo {

class AetherByteTransport final : public apptraverse::IByteTransport {
 public:
  AetherByteTransport(ChatAetherRuntime& runtime,
                      std::string local_endpoint_uid);
  ~AetherByteTransport() override = default;

  AetherByteTransport(AetherByteTransport const&) = delete;
  AetherByteTransport& operator=(AetherByteTransport const&) = delete;

  std::string const& local_endpoint_uid() const override {
    return local_endpoint_uid_;
  }

  void Send(std::string const& destination_endpoint,
            std::vector<std::uint8_t> bytes) override;

  void BindReceive(void* ctx, ReceiveFn fn) override;
  void ClearReceive() override;

  void OnFrame(std::string const& source_uid,
               std::vector<std::uint8_t> bytes);

 private:
  ChatAetherRuntime& runtime_;
  std::string local_endpoint_uid_;

  std::mutex mu_;
  void* receive_ctx_{nullptr};
  ReceiveFn receive_fn_{nullptr};
};

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_AETHER_BYTE_TRANSPORT_H_
