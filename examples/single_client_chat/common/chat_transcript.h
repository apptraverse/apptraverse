#ifndef APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_
#define APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_

#include <string>

#include "apptraverse/chat.h"
#include "apptraverse/chat_entry.h"

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
    auto loaded = entry;
    loaded.Load();
    if (!loaded.is_loaded()) {
      continue;
    }
    if (loaded->GetClassId() == JoinClientEntry::kClassId) {
      auto& join = static_cast<JoinClientEntry&>(*loaded);
      join.client.Load();
      text += "* ";
      text += join.client->name;
      text += " joined\n";
    } else if (loaded->GetClassId() == MessageEntry::kClassId) {
      auto& message = static_cast<MessageEntry&>(*loaded);
      message.author.Load();
      text += message.author->name;
      text += ": ";
      text += message.text;
      text += "\n";
    }
  }
  return text;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_
