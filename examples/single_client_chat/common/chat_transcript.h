#ifndef APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_
#define APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_

#include <string>

#include "model/chat.h"
#include "model/chat_entry.h"

namespace apptraverse::examples {

// Platform-neutral UTF-8 transcript from the materialized Chat model.
// Does not mutate the model or create events.
inline std::string FormatChatTranscriptUtf8(Chat::ptr const& chat) {
  if (!chat.is_valid()) {
    return {};
  }
  chat.Load();
  if (!chat.is_loaded()) {
    return {};
  }

  std::string text;
  for (auto const& entry : chat->entries) {
    if (!entry.client.is_valid()) {
      continue;
    }
    auto client = entry.client;
    client.Load();
    if (!client.is_loaded()) {
      continue;
    }
    if (entry.kind == ChatEntryKind::kJoined) {
      text += "* ";
      text += client->name;
      text += " joined\n";
    } else if (entry.kind == ChatEntryKind::kMessage) {
      text += client->name;
      text += ": ";
      text += entry.text;
      text += "\n";
    }
  }
  return text;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_
