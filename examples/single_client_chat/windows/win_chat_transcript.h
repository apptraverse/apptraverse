#ifndef APPTRAVERSE_EXAMPLES_WIN_CHAT_TRANSCRIPT_H_
#define APPTRAVERSE_EXAMPLES_WIN_CHAT_TRANSCRIPT_H_

#include <cstdio>
#include <ctime>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../common/chat_presentation.h"

namespace apptraverse::examples {

// Local wall-clock HH:mm:ss from EventRecord timestamp_us (no fractional seconds).
inline std::string FormatLocalHhMmSs(std::uint64_t timestamp_us) {
  auto const secs = static_cast<std::time_t>(timestamp_us / 1000000ULL);
  std::tm local{};
  localtime_s(&local, &secs);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", local.tm_hour, local.tm_min,
                local.tm_sec);
  return buf;
}

// Runtime-only UI delivery cache (not part of model / wire / snapshot schema).
class WindowsTranscriptDeliveryCache {
 public:
  // Call once with the first UI snapshot of this process: those Message IDs
  // are treated as persisted history (no [N ms] for the life of the process).
  void SeedPersistedFromSnapshot(
      chat::ChatPresentationSnapshot const& snapshot) {
    if (seeded_) {
      return;
    }
    seeded_ = true;
    for (auto const& item : snapshot.timeline) {
      if (item.kind != chat::ChatTimelineItemKind::kMessage) {
        continue;
      }
      if (item.event_obj_id == 0) {
        continue;
      }
      persisted_.insert(item.event_obj_id);
    }
  }

  bool seeded() const { return seeded_; }

  // Record first UI apply latency for a Message event. Subsequent calls keep
  // the cached value. Negative deltas clamp to 0. Returns nullopt for Join /
  // persisted / missing id (caller omits [N ms]).
  std::optional<std::uint64_t> DeliveryMsForMessage(
      std::uint32_t event_obj_id, std::uint64_t event_timestamp_us,
      std::uint64_t apply_time_us) {
    if (event_obj_id == 0) {
      return std::nullopt;
    }
    if (persisted_.count(event_obj_id) != 0) {
      return std::nullopt;
    }
    auto it = delivery_ms_.find(event_obj_id);
    if (it != delivery_ms_.end()) {
      return it->second;
    }
    std::uint64_t delta_us = 0;
    if (apply_time_us >= event_timestamp_us) {
      delta_us = apply_time_us - event_timestamp_us;
    }
    auto const ms = delta_us / 1000ULL;  // integer floor
    delivery_ms_[event_obj_id] = ms;
    return ms;
  }

  std::optional<std::uint64_t> CachedDeliveryMs(
      std::uint32_t event_obj_id) const {
    auto it = delivery_ms_.find(event_obj_id);
    if (it == delivery_ms_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

 private:
  bool seeded_{false};
  std::unordered_set<std::uint32_t> persisted_;
  std::unordered_map<std::uint32_t, std::uint64_t> delivery_ms_;
};

// Windows-only transcript. Pass delivery_cache + apply_time_us for live Message
// latency; omit cache for format-only callers that do not need [N ms].
inline std::string FormatWindowsChatPresentationUtf8(
    chat::ChatPresentationSnapshot const& snapshot,
    WindowsTranscriptDeliveryCache* delivery_cache = nullptr,
    std::optional<std::uint64_t> apply_time_us = std::nullopt) {
  std::string text;
  for (auto const& item : snapshot.timeline) {
    if (item.kind == chat::ChatTimelineItemKind::kJoined) {
      if (item.author.display_name.empty()) {
        continue;
      }
      text += '[';
      text += FormatLocalHhMmSs(item.timestamp_us);
      text += "] * ";
      text += item.author.display_name;
      text += " joined\n";
    } else if (item.kind == chat::ChatTimelineItemKind::kMessage) {
      text += '[';
      text += FormatLocalHhMmSs(item.timestamp_us);
      text += "] ";
      text += item.author.display_name;
      text += ": ";
      text += item.text;
      if (delivery_cache != nullptr && apply_time_us.has_value()) {
        auto const ms = delivery_cache->DeliveryMsForMessage(
            item.event_obj_id, item.timestamp_us, *apply_time_us);
        if (ms.has_value()) {
          text += " [";
          text += std::to_string(*ms);
          text += " ms]";
        }
      } else if (delivery_cache != nullptr) {
        // Re-render with cached latencies only (no new measurements).
        if (auto ms = delivery_cache->CachedDeliveryMs(item.event_obj_id)) {
          text += " [";
          text += std::to_string(*ms);
          text += " ms]";
        }
      }
      text += "\n";
    }
  }
  return text;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_WIN_CHAT_TRANSCRIPT_H_
