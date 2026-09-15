#include <cstdint>
#include <iostream>
#include <random>
#include <vector>

#include "aether_stream_frame.h"

#define CHECK(cond)                                                           \
  do {                                                                        \
    if (!(cond)) {                                                            \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"        \
                << __LINE__ << '\n';                                          \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

using namespace apptraverse::example::chat_demo;

int main() {
  // 1. Empty application payload round trip
  {
    std::vector<std::uint8_t> empty_payload;
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, empty_payload);
    CHECK(encoded.size() == 10);

    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(DecodeAetherFrame(encoded, out_kind, out_payload));
    CHECK(out_kind == AetherFrameKind::kApplication);
    CHECK(out_payload.empty());
  }

  // 2. Binary payload containing 00 FF 'A' 'T' 'H' 'B' 01 01 round trips as APPLICATION, not heartbeat
  {
    std::vector<std::uint8_t> tricky_payload = {
        0x00, 0xFF, 'A', 'T', 'H', 'B', 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, tricky_payload);

    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(DecodeAetherFrame(encoded, out_kind, out_payload));
    CHECK(out_kind == AetherFrameKind::kApplication);
    CHECK(out_payload == tricky_payload);
  }

  // 3. 16 KiB random application payload exact round trip
  {
    std::vector<std::uint8_t> rand_payload(16 * 1024);
    std::mt19937 rng(12345);
    for (auto& b : rand_payload) {
      b = static_cast<std::uint8_t>(rng() & 0xFF);
    }
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, rand_payload);

    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(DecodeAetherFrame(encoded, out_kind, out_payload));
    CHECK(out_kind == AetherFrameKind::kApplication);
    CHECK(out_payload == rand_payload);
  }

  // 4. Ping nonce exact
  {
    std::uint64_t const nonce = 0x0123456789ABCDEFULL;
    auto nonce_bytes = EncodeHeartbeatNonce(nonce);
    auto encoded = EncodeAetherFrame(AetherFrameKind::kHeartbeatPing, nonce_bytes);

    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(DecodeAetherFrame(encoded, out_kind, out_payload));
    CHECK(out_kind == AetherFrameKind::kHeartbeatPing);
    CHECK(DecodeHeartbeatNonce(out_payload) == nonce);
  }

  // 5. Pong nonce exact
  {
    std::uint64_t const nonce = 0xFEDCBA9876543210ULL;
    auto nonce_bytes = EncodeHeartbeatNonce(nonce);
    auto encoded = EncodeAetherFrame(AetherFrameKind::kHeartbeatPong, nonce_bytes);

    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(DecodeAetherFrame(encoded, out_kind, out_payload));
    CHECK(out_kind == AetherFrameKind::kHeartbeatPong);
    CHECK(DecodeHeartbeatNonce(out_payload) == nonce);
  }

  // 6. Wrong magic rejected
  {
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, {});
    encoded[0] = 'X';
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(encoded, out_kind, out_payload));
  }

  // 7. Wrong version rejected
  {
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, {});
    encoded[4] = 2;  // version 2
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(encoded, out_kind, out_payload));
  }

  // 8. Unknown kind rejected
  {
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, {});
    encoded[5] = 99;  // unknown kind
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(encoded, out_kind, out_payload));
  }

  // 9. Truncated header rejected (< 10 bytes)
  {
    std::vector<std::uint8_t> truncated = {'A', 'T', 'R', 'N', 1, 1, 0, 0, 0};  // 9 bytes
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(truncated, out_kind, out_payload));
  }

  // 10. Declared length too large rejected
  {
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, {});
    encoded[6] = 5;  // declared size = 5, but total vector is only 10 bytes
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(encoded, out_kind, out_payload));
  }

  // 11. Declared length too small + trailing bytes rejected
  {
    std::vector<std::uint8_t> payload = {'A', 'B', 'C'};
    auto encoded = EncodeAetherFrame(AetherFrameKind::kApplication, payload);
    encoded.push_back('X');  // trailing byte: size is 14, but declared is 3 (10+3 != 14)
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(encoded, out_kind, out_payload));
  }

  // 12. Heartbeat payload != 8 rejected
  {
    std::vector<std::uint8_t> bad_hb_payload = {1, 2, 3, 4};  // 4 bytes instead of 8
    auto encoded = EncodeAetherFrame(AetherFrameKind::kHeartbeatPing, bad_hb_payload);
    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(encoded, out_kind, out_payload));
  }

  // 13. Application payload > 16 MiB rejected
  {
    std::vector<std::uint8_t> fake_huge(10);
    fake_huge[0] = 'A';
    fake_huge[1] = 'T';
    fake_huge[2] = 'R';
    fake_huge[3] = 'N';
    fake_huge[4] = 1;
    fake_huge[5] = static_cast<std::uint8_t>(AetherFrameKind::kApplication);
    // declare 17 MiB
    std::uint32_t const size = 17 * 1024 * 1024;
    fake_huge[6] = static_cast<std::uint8_t>((size >> 0) & 0xFF);
    fake_huge[7] = static_cast<std::uint8_t>((size >> 8) & 0xFF);
    fake_huge[8] = static_cast<std::uint8_t>((size >> 16) & 0xFF);
    fake_huge[9] = static_cast<std::uint8_t>((size >> 24) & 0xFF);
    fake_huge.resize(10 + size);  // allocate vector of declared size

    AetherFrameKind out_kind{};
    std::vector<std::uint8_t> out_payload;
    CHECK(!DecodeAetherFrame(fake_huge, out_kind, out_payload));
  }

  std::cout << "aether_stream_frame_test passed!\n";
  return 0;
}
