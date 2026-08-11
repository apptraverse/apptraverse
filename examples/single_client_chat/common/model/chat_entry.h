#ifndef APPTRAVERSE_CHAT_ENTRY_H_
#define APPTRAVERSE_CHAT_ENTRY_H_

#include <cstdint>
#include <string>

#include "aether-miscpp/reflect/reflect.h"

#include "model/client.h"

namespace apptraverse {

enum class ChatEntryKind : std::uint8_t {
  kJoined = 0,
  kMessage = 1,
};

// Materialized chat line. Value type — not an ae::Obj.
struct ChatEntry {
  ChatEntryKind kind{ChatEntryKind::kJoined};
  Client::ptr client;
  std::string text;

  AE_REFLECT_MEMBERS(kind, client, text)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_ENTRY_H_
