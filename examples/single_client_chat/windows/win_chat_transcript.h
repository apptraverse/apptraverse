#ifndef APPTRAVERSE_EXAMPLES_WIN_CHAT_TRANSCRIPT_H_
#define APPTRAVERSE_EXAMPLES_WIN_CHAT_TRANSCRIPT_H_

#include <cstdio>
#include <ctime>
#include <cstdint>
#include <string>

#include "../common/chat_presentation.h"

namespace apptraverse::examples {

// Local wall-clock HH:mm:ss.SSS from EventRecord timestamp_us (no Now()).
inline std::string FormatLocalHhMmSsMmm(std::uint64_t timestamp_us) {
  auto const secs = static_cast<std::time_t>(timestamp_us / 1000000ULL);
  auto const ms =
      static_cast<unsigned>((timestamp_us / 1000ULL) % 1000ULL);
  std::tm local{};
  localtime_s(&local, &secs);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03u", local.tm_hour,
                local.tm_min, local.tm_sec, ms);
  return buf;
}

// Windows-only transcript lines with millisecond timestamps.
inline std::string FormatWindowsChatPresentationUtf8(
    chat::ChatPresentationSnapshot const& snapshot) {
  std::string text;
  for (auto const& item : snapshot.timeline) {
    if (item.kind == chat::ChatTimelineItemKind::kJoined) {
      if (item.author.display_name.empty()) {
        continue;
      }
      text += '[';
      text += FormatLocalHhMmSsMmm(item.timestamp_us);
      text += "] * ";
      text += item.author.display_name;
      text += " joined\n";
    } else if (item.kind == chat::ChatTimelineItemKind::kMessage) {
      text += '[';
      text += FormatLocalHhMmSsMmm(item.timestamp_us);
      text += "] ";
      text += item.author.display_name;
      text += ": ";
      text += item.text;
      text += "\n";
    }
  }
  return text;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_WIN_CHAT_TRANSCRIPT_H_
