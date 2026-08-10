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

}  // namespace

int main() {
  TestOneFrame();
  TestByteByByte();
  TestTwoFramesOneBuffer();
  TestIncompleteSecondFrame();
  std::cout << "aether_p2p_framing_test OK\n";
  return 0;
}
