#ifndef APPTRAVERSE_EVENT_TRANSPORT_H_
#define APPTRAVERSE_EVENT_TRANSPORT_H_

#include <cstdint>
#include <vector>

#include "aether/obj/obj_id.h"

#include "apptraverse/event_identity.h"

namespace apptraverse {

/**
 * \brief Serialized concrete event object independent of any Domain.
 *
 * A transport adapter must deliver an independent event object into the
 * receiving Domain. This payload is the logical event body for that purpose.
 * It intentionally has no dependency on Æther networking.
 */
struct EventObjectPayload {
  std::vector<std::uint32_t> class_ids;
  struct Layer {
    std::uint32_t class_id{};
    std::uint8_t version{};
    std::vector<std::uint8_t> data;
  };
  std::vector<Layer> layers;
};

struct EventTransportMessage {
  ae::ObjId target_node_id{};
  EventIdentity identity{};
  std::uint64_t logical_time{0};
  EventObjectPayload event_object{};
};

struct EventConfirmation {
  ae::ObjId target_node_id{};
  EventIdentity identity{};
};

class IEventTransportReceiver {
 public:
  virtual ~IEventTransportReceiver() = default;
  virtual void OnEvent(EventTransportMessage message) = 0;
  virtual void OnConfirmation(EventConfirmation confirmation) = 0;
};

/**
 * \brief Transport-independent delivery endpoint.
 *
 * Future adapters may use Æther messaging, TCP, WebSocket, IPC, or tests.
 * Operations are infallible under the ideal-condition assumptions of this
 * stage.
 */
class IEventTransport {
 public:
  virtual ~IEventTransport() = default;
  virtual void SetReceiver(IEventTransportReceiver* receiver) = 0;
  virtual void SendEvent(EventTransportMessage message) = 0;
  virtual void SendConfirmation(EventConfirmation confirmation) = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_TRANSPORT_H_
