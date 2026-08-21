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
  using apptraverse::examples::FormatLocalHhMmSsMmm;
  using apptraverse::examples::FormatWindowsChatPresentationUtf8;

  // Deterministic: 1_700_000_000_123_456 us → ms residue 123 (not rounded to 0).
  constexpr std::uint64_t kTsUs = 1700000000123456ULL;
  auto const time_part = FormatLocalHhMmSsMmm(kTsUs);
  CHECK(std::regex_match(time_part, std::regex(R"(\d{2}:\d{2}:\d{2}\.\d{3})")));
  CHECK(time_part.size() >= 4);
  CHECK(time_part.substr(time_part.size() - 3) == "123");

  ChatPresentationSnapshot snap{};
  ChatTimelineItemView msg{};
  msg.kind = ChatTimelineItemKind::kMessage;
  msg.direction = ChatMessageDirection::kLocal;
  msg.author.display_name = "Windows";
  msg.text = "TEST_TIMESTAMP";
  msg.timestamp_us = kTsUs;
  snap.timeline.push_back(msg);

  ChatTimelineItemView join{};
  join.kind = ChatTimelineItemKind::kJoined;
  join.author.display_name = "Windows";
  join.timestamp_us = kTsUs;
  snap.timeline.push_back(join);

  auto const line = FormatWindowsChatPresentationUtf8(snap);
  auto const expected_prefix = "[" + time_part + "] ";
  CHECK(line.find(expected_prefix + "Windows: TEST_TIMESTAMP\n") !=
        std::string::npos);
  CHECK(line.find(expected_prefix + "* Windows joined\n") != std::string::npos);
  CHECK(line.find("Windows: TEST_TIMESTAMP") != std::string::npos);
  // Author/text unchanged aside from timestamp prefix.
  CHECK(line.find("WindowsX") == std::string::npos);
  CHECK(line.find("TEST_TIMESTAMP_X") == std::string::npos);

  std::cout << "win_chat_transcript_test OK sample="
            << expected_prefix << "Windows: TEST_TIMESTAMP\n";
  return 0;
}
