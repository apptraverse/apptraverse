#ifndef APPTRAVERSE_EXAMPLES_EVENT_DRIVEN_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_EVENT_DRIVEN_RUNTIME_H_

#include <filesystem>
#include <optional>
#include <string>

#include "aether/types/uid.h"

namespace apptraverse::examples {

struct EventDrivenCliOptions {
  std::filesystem::path state_dir{"state"};
  std::string aether_client_name;
  bool distill{false};
  bool print_aether_uid{false};
  std::optional<ae::Uid> peer;
  std::optional<std::filesystem::path> peer_inbox;
  std::optional<std::filesystem::path> latency_trace;
  std::string window_title_suffix;
};

// Distill model into <state>/model and prepare empty <state>/aether.
int DistillEventDriven(EventDrivenCliOptions const& options);

// Run UI / business / network event-driven pipeline.
int RunEventDriven(EventDrivenCliOptions const& options);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_EVENT_DRIVEN_RUNTIME_H_
