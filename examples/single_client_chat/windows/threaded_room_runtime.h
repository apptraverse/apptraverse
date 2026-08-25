#ifndef APPTRAVERSE_EXAMPLES_THREADED_ROOM_RUNTIME_H_
#define APPTRAVERSE_EXAMPLES_THREADED_ROOM_RUNTIME_H_

#include <filesystem>
#include <optional>
#include <string>

#include "aether/types/uid.h"

#include "model/chat_room_local_state.h"

namespace apptraverse::examples {

struct ThreadedRoomCliOptions {
  std::filesystem::path state_dir{"state"};
  std::string aether_client_name;
  std::optional<ChatRoomRole> role;
  std::string name;
  std::string title;
  // Client-only optional host seed; invalid for host.
  std::optional<ae::Uid> host_uid;
  // Automation: submit once after room CanSendChat (Active).
  std::optional<std::string> send_after_active;
  bool print_aether_uid{false};
};

// Three-thread RAM-only Host/Client room chat (UI / Business / Network).
// AppTraverse model is ae::RamDomainStorage only; state_dir is Aether only.
int RunThreadedRoomRuntime(ThreadedRoomCliOptions const& options);

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_THREADED_ROOM_RUNTIME_H_
