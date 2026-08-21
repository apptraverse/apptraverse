#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>

#include "win_chat_transcript.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  using apptraverse::chat::ChatMessageDirection;
  using apptraverse::chat::ChatPresentationSnapshot;
  using apptraverse::chat::ChatTimelineItemKind;
  using apptraverse::chat::ChatTimelineItemView;
  using apptraverse::examples::FormatLocalHhMmSs;
  using apptraverse::examples::FormatWindowsChatPresentationUtf8;
  using apptraverse::examples::WindowsTranscriptDeliveryCache;

  // Deterministic wall clock: construct timestamp_us for 12:34:56 local.
  // Use a fixed UTC micros that localtime_s maps; verify HH:MM:SS shape and
  // that fractional seconds are absent.
  std::tm want{};
  want.tm_year = 2024 - 1900;
  want.tm_mon = 5;
  want.tm_mday = 15;
  want.tm_hour = 12;
  want.tm_min = 34;
  want.tm_sec = 56;
  want.tm_isdst = -1;
  auto const secs = _mktime64(&want);
  CHECK(secs != -1);
  constexpr std::uint64_t kFracUs = 123456ULL;
  std::uint64_t const kTsUs =
      static_cast<std::uint64_t>(secs) * 1000000ULL + kFracUs;

  auto const time_part = FormatLocalHhMmSs(kTsUs);
  CHECK(std::regex_match(time_part, std::regex(R"(\d{2}:\d{2}:\d{2})")));
  CHECK(time_part.find('.') == std::string::npos);
  CHECK(time_part == "12:34:56");

  ChatTimelineItemView msg{};
  msg.kind = ChatTimelineItemKind::kMessage;
  msg.direction = ChatMessageDirection::kLocal;
  msg.author.display_name = "TestUser";
  msg.text = "hello";
  msg.timestamp_us = kTsUs;
  msg.event_obj_id = 42;

  ChatTimelineItemView join{};
  join.kind = ChatTimelineItemKind::kJoined;
  join.author.display_name = "TestUser";
  join.timestamp_us = kTsUs;
  join.event_obj_id = 7;

  WindowsTranscriptDeliveryCache cache;
  ChatPresentationSnapshot empty_seed{};
  cache.SeedPersistedFromSnapshot(empty_seed);

  ChatPresentationSnapshot snap{};
  snap.timeline.push_back(msg);
  snap.timeline.push_back(join);

  std::uint64_t const kApplyUs = kTsUs + 43900ULL;
  auto const line =
      FormatWindowsChatPresentationUtf8(snap, &cache, kApplyUs);
  auto const expected_msg =
      "[" + time_part + "] TestUser: hello [43 ms]\n";
  auto const expected_join = "[" + time_part + "] * TestUser joined\n";
  CHECK(line.find(expected_msg) != std::string::npos);
  CHECK(line.find(expected_join) != std::string::npos);
  CHECK(line.find(".SSS") == std::string::npos);
  CHECK(line.find(".123") == std::string::npos);

  // Later re-render keeps cached 43 ms (apply much later).
  auto const again =
      FormatWindowsChatPresentationUtf8(snap, &cache, kApplyUs + 5'000'000ULL);
  CHECK(again.find("[43 ms]") != std::string::npos);
  CHECK(again.find("[5043 ms]") == std::string::npos);

  // Delivery-cache: independent event IDs; negative → 0 ms.
  WindowsTranscriptDeliveryCache cache2;
  cache2.SeedPersistedFromSnapshot(empty_seed);
  ChatTimelineItemView msg2 = msg;
  msg2.event_obj_id = 99;
  msg2.text = "other";
  ChatPresentationSnapshot snap2{};
  snap2.timeline.push_back(msg);
  snap2.timeline.push_back(msg2);
  auto const d1 =
      FormatWindowsChatPresentationUtf8(snap2, &cache2, kTsUs + 10000);
  CHECK(d1.find("hello [10 ms]") != std::string::npos);
  CHECK(d1.find("other [10 ms]") != std::string::npos);

  auto const neg = cache2.DeliveryMsForMessage(100, kTsUs + 5000, kTsUs);
  CHECK(neg.has_value());
  CHECK(*neg == 0);

  // Persisted history: seeded Message IDs omit [N ms].
  WindowsTranscriptDeliveryCache cache3;
  ChatPresentationSnapshot seed_snap{};
  ChatTimelineItemView old_msg = msg;
  old_msg.event_obj_id = 55;
  old_msg.text = "old message";
  seed_snap.timeline.push_back(old_msg);
  cache3.SeedPersistedFromSnapshot(seed_snap);
  ChatPresentationSnapshot after{};
  after.timeline.push_back(old_msg);
  auto const persisted =
      FormatWindowsChatPresentationUtf8(after, &cache3, kTsUs + 9'000'000ULL);
  CHECK(persisted.find("old message\n") != std::string::npos);
  CHECK(persisted.find("[") != std::string::npos);  // timestamp brackets
  CHECK(persisted.find(" ms]") == std::string::npos);

  // Snapshot without change keeps cached latency map size.
  auto const cached = cache.CachedDeliveryMs(42);
  CHECK(cached.has_value());
  CHECK(*cached == 43);
  (void)FormatWindowsChatPresentationUtf8(snap, &cache, std::nullopt);
  CHECK(cache.CachedDeliveryMs(42) == cached);

  std::cout << "win_chat_transcript_test OK sample=" << expected_msg;
  return 0;
}
