#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether_p2p_framing.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

using apptraverse::examples::AetherP2pFrameDecoder;
using apptraverse::examples::EncodeAetherP2pFrame;

std::vector<std::uint8_t> Bytes(std::string const& text) {
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> Pattern(std::size_t size, std::uint8_t seed = 0) {
  std::vector<std::uint8_t> out(size);
  for (std::size_t i = 0; i < size; ++i) {
    out[i] = static_cast<std::uint8_t>((i + seed) & 0xffu);
  }
  return out;
}

void FeedChunks(AetherP2pFrameDecoder& decoder,
                std::vector<std::uint8_t> const& data, std::size_t chunk,
                std::vector<std::vector<std::uint8_t>>* frames) {
  if (chunk == 0) {
    chunk = data.size() == 0 ? 1 : data.size();
  }
  for (std::size_t off = 0; off < data.size(); off += chunk) {
    auto const n = (std::min)(chunk, data.size() - off);
    decoder.Append(data.data() + off, n);
    decoder.Drain([&](auto const& p) { frames->push_back(p); });
  }
  if (data.empty()) {
    decoder.Drain([&](auto const& p) { frames->push_back(p); });
  }
}

void TestOneFrame() {
  auto const payload = Bytes("hello");
  auto const frame = EncodeAetherP2pFrame(payload);
  AetherP2pFrameDecoder decoder;
  std::vector<std::vector<std::uint8_t>> frames;
  decoder.Append(frame.data(), frame.size());
  decoder.Drain([&](auto const& p) { frames.push_back(p); });
  CHECK(frames.size() == 1);
  CHECK(frames[0] == payload);
  CHECK(decoder.buffer().empty());
}

void TestByteByByte() {
  auto const payload = Bytes("abc");
  auto const frame = EncodeAetherP2pFrame(payload);
  AetherP2pFrameDecoder decoder;
  std::vector<std::vector<std::uint8_t>> frames;
  for (auto byte : frame) {
    decoder.Append(&byte, 1);
    decoder.Drain([&](auto const& p) { frames.push_back(p); });
  }
  CHECK(frames.size() == 1);
  CHECK(frames[0] == payload);
}

void TestTwoFramesOneBuffer() {
  auto const a = Bytes("one");
  auto const b = Bytes("two");
  auto frame = EncodeAetherP2pFrame(a);
  auto const second = EncodeAetherP2pFrame(b);
  frame.insert(frame.end(), second.begin(), second.end());
  AetherP2pFrameDecoder decoder;
  std::vector<std::vector<std::uint8_t>> frames;
  decoder.Append(frame.data(), frame.size());
  decoder.Drain([&](auto const& p) { frames.push_back(p); });
  CHECK(frames.size() == 2);
  CHECK(frames[0] == a);
  CHECK(frames[1] == b);
}

void TestIncompleteSecondFrame() {
  auto const a = Bytes("complete");
  auto const b = Bytes("partial");
  auto buffer = EncodeAetherP2pFrame(a);
  auto const second = EncodeAetherP2pFrame(b);
  buffer.insert(buffer.end(), second.begin(), second.begin() + 3);
  AetherP2pFrameDecoder decoder;
  std::vector<std::vector<std::uint8_t>> frames;
  decoder.Append(buffer.data(), buffer.size());
  decoder.Drain([&](auto const& p) { frames.push_back(p); });
  CHECK(frames.size() == 1);
  CHECK(frames[0] == a);
  CHECK(decoder.buffer().size() == 3);
}

void TestChunkedReassemblyMatrix() {
  static constexpr std::size_t kSizes[] = {0,    1,    1023,  1024, 1025,
                                          4096, 5500, 16384, 65536};
  static constexpr std::size_t kChunks[] = {1, 64, 256, 1024, 1500};
  for (auto size : kSizes) {
    auto const payload = Pattern(size, 7);
    auto const frame = EncodeAetherP2pFrame(payload);
    for (auto chunk : kChunks) {
      AetherP2pFrameDecoder decoder;
      std::vector<std::vector<std::uint8_t>> frames;
      FeedChunks(decoder, frame, chunk, &frames);
      CHECK(frames.size() == 1);
      CHECK(frames[0] == payload);
      CHECK(decoder.buffer().empty());
    }
  }
}

void TestTwoFramesSplitAcrossArbitraryBoundaries() {
  auto const a = Pattern(5500, 1);
  auto const b = Pattern(16384, 2);
  auto buffer = EncodeAetherP2pFrame(a);
  auto const second = EncodeAetherP2pFrame(b);
  buffer.insert(buffer.end(), second.begin(), second.end());

  // Boundary inside frame1, at exact frame boundary, and inside frame2.
  static constexpr std::size_t kCuts[] = {3, 100, 5504, 5505, 6000, 10000};
  for (auto cut : kCuts) {
    if (cut >= buffer.size()) {
      continue;
    }
    AetherP2pFrameDecoder decoder;
    std::vector<std::vector<std::uint8_t>> frames;
    decoder.Append(buffer.data(), cut);
    decoder.Drain([&](auto const& p) { frames.push_back(p); });
    decoder.Append(buffer.data() + cut, buffer.size() - cut);
    decoder.Drain([&](auto const& p) { frames.push_back(p); });
    CHECK(frames.size() == 2);
    CHECK(frames[0] == a);
    CHECK(frames[1] == b);
  }

  // One Append that contains end of frame1 + start of frame2 mid-chunk feed.
  AetherP2pFrameDecoder decoder;
  std::vector<std::vector<std::uint8_t>> frames;
  FeedChunks(decoder, buffer, 777, &frames);
  CHECK(frames.size() == 2);
  CHECK(frames[0] == a);
  CHECK(frames[1] == b);
}

}  // namespace

int main() {
  TestOneFrame();
  TestByteByByte();
  TestTwoFramesOneBuffer();
  TestIncompleteSecondFrame();
  TestChunkedReassemblyMatrix();
  TestTwoFramesSplitAcrossArbitraryBoundaries();
  std::cout << "aether_p2p_framing_test OK\n";
  return 0;
}
