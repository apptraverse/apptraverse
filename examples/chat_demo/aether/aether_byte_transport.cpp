#include "aether_byte_transport.h"

#include <cassert>
#include <utility>

namespace apptraverse::example::chat_demo {

AetherByteTransport::AetherByteTransport(
    IAetherFrameEndpoint& endpoint,
    std::string local_endpoint_uid,
    ModelDispatch dispatch_to_model)
    : endpoint_{endpoint},
      local_endpoint_uid_{std::move(local_endpoint_uid)},
      receive_binding_{std::make_shared<ReceiveBinding>()} {
  assert(dispatch_to_model != nullptr && "ModelDispatch is required");
  receive_binding_->dispatch = std::move(dispatch_to_model);

  std::weak_ptr<ReceiveBinding> weak_binding = receive_binding_;
  endpoint_.SetFrameCallback(
      [weak_binding](std::string source_uid, std::vector<std::uint8_t> bytes) {
        auto binding = weak_binding.lock();
        if (!binding) {
          return;
        }
        ModelDispatch dispatch;
        {
          std::lock_guard<std::mutex> lock{binding->mu};
          if (!binding->active) {
            return;
          }
          dispatch = binding->dispatch;
        }
        if (!dispatch) {
          return;
        }
        dispatch([weak_binding, source_uid = std::move(source_uid),
                  bytes = std::move(bytes)]() mutable {
          auto state = weak_binding.lock();
          if (!state) {
            return;
          }
          ReceiveFn fn = nullptr;
          void* ctx = nullptr;
          {
            std::lock_guard<std::mutex> lock{state->mu};
            if (!state->active) {
              return;
            }
            ctx = state->receive_ctx;
            fn = state->receive_fn;
          }
          if (fn != nullptr) {
            fn(ctx, source_uid, bytes);
          }
        });
      });
}

AetherByteTransport::~AetherByteTransport() {
  endpoint_.SetFrameCallback({});
  if (receive_binding_) {
    {
      std::lock_guard<std::mutex> lock{receive_binding_->mu};
      receive_binding_->active = false;
      receive_binding_->receive_ctx = nullptr;
      receive_binding_->receive_fn = nullptr;
      receive_binding_->availability_ctx = nullptr;
      receive_binding_->availability_fn = nullptr;
    }
    receive_binding_.reset();
  }
}

void AetherByteTransport::Send(std::string const& destination_endpoint,
                               std::vector<std::uint8_t> bytes) {
  endpoint_.Send(destination_endpoint, std::move(bytes));
}

void AetherByteTransport::BindReceive(void* ctx, ReceiveFn fn) {
  if (!receive_binding_) {
    return;
  }
  std::lock_guard<std::mutex> lock{receive_binding_->mu};
  if (!receive_binding_->active) {
    return;
  }
  receive_binding_->receive_ctx = ctx;
  receive_binding_->receive_fn = fn;
}

void AetherByteTransport::ClearReceive() {
  if (!receive_binding_) {
    return;
  }
  std::lock_guard<std::mutex> lock{receive_binding_->mu};
  receive_binding_->receive_ctx = nullptr;
  receive_binding_->receive_fn = nullptr;
}

EndpointAvailability AetherByteTransport::FromPresence(PeerPresence presence) {
  switch (presence) {
    case PeerPresence::kOnline:
      return EndpointAvailability::Online;
    case PeerPresence::kOffline:
      return EndpointAvailability::Offline;
    case PeerPresence::kUnknown:
    case PeerPresence::kConnecting:
      return EndpointAvailability::Unknown;
  }
  return EndpointAvailability::Unknown;
}

EndpointAvailability AetherByteTransport::Availability(
    std::string const& endpoint) const {
  auto const it = availability_.find(endpoint);
  if (it == availability_.end()) {
    return EndpointAvailability::Unknown;
  }
  return it->second;
}

void AetherByteTransport::BindAvailability(void* ctx, AvailabilityFn fn) {
  if (!receive_binding_) {
    return;
  }
  std::lock_guard<std::mutex> lock{receive_binding_->mu};
  if (!receive_binding_->active) {
    return;
  }
  receive_binding_->availability_ctx = ctx;
  receive_binding_->availability_fn = fn;
}

void AetherByteTransport::ClearAvailability() {
  if (!receive_binding_) {
    return;
  }
  std::lock_guard<std::mutex> lock{receive_binding_->mu};
  receive_binding_->availability_ctx = nullptr;
  receive_binding_->availability_fn = nullptr;
}

void AetherByteTransport::NotePeerPresence(std::string const& peer_uid,
                                           PeerPresence presence) {
  if (peer_uid.empty() || !receive_binding_) {
    return;
  }
  auto const next = FromPresence(presence);
  auto const it = availability_.find(peer_uid);
  if (it != availability_.end() && it->second == next) {
    return;
  }
  availability_[peer_uid] = next;

  AvailabilityFn fn = nullptr;
  void* ctx = nullptr;
  {
    std::lock_guard<std::mutex> lock{receive_binding_->mu};
    if (!receive_binding_->active) {
      return;
    }
    fn = receive_binding_->availability_fn;
    ctx = receive_binding_->availability_ctx;
  }
  if (fn != nullptr) {
    // SharedSyncRuntime::AvailabilityThunk only wakes; it must not send.
    fn(ctx, peer_uid, next);
  }
}

}  // namespace apptraverse::example::chat_demo
