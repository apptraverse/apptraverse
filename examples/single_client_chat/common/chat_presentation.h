#ifndef APPTRAVERSE_EXAMPLES_CHAT_PRESENTATION_H_
#define APPTRAVERSE_EXAMPLES_CHAT_PRESENTATION_H_

#include <cstdint>
#include <string>
#include <vector>

namespace apptraverse::examples {

enum class ChatTimelineItemKind : std::uint8_t {
  kJoined = 0,
  kMessage = 1,
};

enum class ChatMessageDirection : std::uint8_t {
  kUnknown = 0,
  kLocal = 1,
  kRemote = 2,
};

struct ChatParticipantView {
  std::string display_name;
  std::uint32_t client_obj_id{};
};

struct ChatTimelineItemView {
  ChatTimelineItemKind kind{ChatTimelineItemKind::kJoined};
  ChatMessageDirection direction{ChatMessageDirection::kUnknown};
  ChatParticipantView author;
  std::string text;
  std::uint32_t event_obj_id{};
  std::uint64_t timestamp_us{};
};

struct ChatPeerStatusView {
  std::string remote_uid;
  bool online{false};
  bool initial_sync_complete{false};
  std::size_t pending_packets{0};
};

struct ChatPresentationSnapshot {
  bool running{false};
  ChatParticipantView local_participant;
  std::vector<ChatTimelineItemView> timeline;
  std::vector<ChatPeerStatusView> peers;
};

}  // namespace apptraverse::examples

#endif