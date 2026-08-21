#ifndef APPTRAVERSE_EXAMPLES_EVENT_DRIVEN_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_EVENT_DRIVEN_RUNTIME_H_

#include <filesystem>
#include <optional>
#include <string>

#include "aether/types/uid.h"

#include "model/chat_room_local_state.h"

namespace apptraverse::examples {

struct EventDrivenCliOptions {
  std::filesystem::path state_dir{"state"};
  std::string aether_client_name;
  bool distill{false};
  bool print_aether_uid{false};
  std::optional<std::filesystem::path> latency_trace;

  // Required for --event-driven-runtime (role / exact title / display name).
  std::optional<ChatRoomRole> role;
  std::string title;
  std::string participant_name;
  // Client-only optional host seed; invalid for host.
  std::optional<ae::Uid> host_uid;
  // Opt-in room control diagnostic trace (runtime-only; not persisted).
  std::optional<std::filesystem::path> room_trace;
};

// Distill model into <state>/model and prepare empty <state>/aether.
int DistillEventDriven(EventDrivenCliOptions const& options);

// Run UI / business / network event-driven pipeline.
int RunEventDriven(EventDrivenCliOptions const& options);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_EVENT_DRIVEN_RUNTIME_H_
