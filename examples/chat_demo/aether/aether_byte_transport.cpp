#include "aether_byte_transport.h"

#include <utility>

namespace apptraverse::example::chat_demo {

AetherByteTransport::AetherByteTransport(ChatAetherRuntime& runtime,
                                         std::string local_endpoint_uid)
    : runtime_{runtime}, local_endpoint_uid_{std::move(local_endpoint_uid)} {
  runtime_.SetFrameCallback(
      [this](std::string source_uid, std::vector<std::uint8_t> bytes) {
        OnFrame(source_uid, std::move(bytes));
      });
}

void AetherByteTransport::Send(std::string const& destination_endpoint,
                               std::vector<std::uint8_t> bytes) {
  runtime_.Send(destination_endpoint, std::move(bytes));
}

void AetherByteTransport::BindReceive(void* ctx, ReceiveFn fn) {
  std::lock_guard<std::mutex> lock{mu_};
  receive_ctx_ = ctx;
  receive_fn_ = fn;
}

void AetherByteTransport::ClearReceive() {
  std::lock_guard<std::mutex> lock{mu_};
  receive_ctx_ = nullptr;
  receive_fn_ = nullptr;
}

void AetherByteTransport::OnFrame(std::string const& source_uid,
                                  std::vector<std::uint8_t> bytes) {
  ReceiveFn fn = nullptr;
  void* ctx = nullptr;
  {
    std::lock_guard<std::mutex> lock{mu_};
    fn = receive_fn_;
    ctx = receive_ctx_;
  }
  if (fn != nullptr) {
    fn(ctx, source_uid, bytes);
  }
}

}  // namespace apptraverse::example::chat_demo
