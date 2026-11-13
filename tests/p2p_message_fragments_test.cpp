#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "apptraverse/p2p_message_fragments.h"

namespace {
#define CHECK(condition)                                                      \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
      std::exit(1);                                                           \
    }                                                                         \
  } while (false)
using Bytes = std::vector<std::uint8_t>;
Bytes Payload(std::size_t size, unsigned seed = 0) {
  Bytes bytes(size);
  for (std::size_t i = 0; i < size; ++i)
    bytes[i] = static_cast<std::uint8_t>(i + seed);
  return bytes;
}
std::vector<Bytes> Fragments(Bytes bytes, std::size_t budget) {
  apptraverse::P2pMessageWriter writer{std::move(bytes)};
  std::vector<Bytes> result;
  while (!writer.done()) {
    auto packet = writer.Next(budget);
    CHECK(!packet.empty() && packet.size() <= budget);
    result.push_back(std::move(packet));
    writer.Advance();
  }
  return result;
}
Bytes Pack(apptraverse::P2pMessageFragment const& fragment) {
  Bytes bytes;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{buffer};
  CHECK(archive.Save(fragment).IsOk());
  return bytes;
}
void LossReorderDuplicatesAndLostBusinessAck() {
  auto original = Payload(1024 * 1024);
  auto packets = Fragments(original, 1022);
  apptraverse::P2pMessageReader reader;
  CHECK(packets.size() > 1000);
  for (std::size_t i = packets.size(); i-- > 0;) {
    if (i % 7 == 0) continue;
    CHECK(!reader.Receive(packets[i], 1));
    CHECK(!reader.Receive(packets[i], 2));
  }
  CHECK(reader.assemblies() == 1 && reader.buffered_bytes() == original.size());
  std::size_t delivered = 0;
  for (auto const& packet : packets) {
    auto result = reader.Receive(packet, 3);
    if (result) {
      CHECK(*result == original);
      ++delivered;
    }
  }
  CHECK(delivered == 1);
  // A lost SharedSync ACK requires actual redelivery, not a transport dedup
  // cache swallowing the retry. SharedSync itself suppresses duplicate Events.
  reader.Reset();
  for (auto const& packet : packets) {
    auto result = reader.Receive(packet, 4);
    if (result) {
      CHECK(*result == original);
      ++delivered;
    }
  }
  CHECK(delivered == 2 && reader.buffered_bytes() == 0);
}
void BoundIncompleteAssembliesAndExpireWithoutTraffic() {
  apptraverse::P2pMessageReader reader{{2048, 2, 4096, 100}};
  auto a = Fragments(Payload(2048, 1), 256);
  auto b = Fragments(Payload(2048, 2), 256);
  auto c = Fragments(Payload(2048, 3), 256);
  CHECK(!reader.Receive(a[0], 0));
  CHECK(!reader.Receive(b[0], 0));
  CHECK(!reader.Receive(c[0], 1));
  CHECK(reader.assemblies() == 2 && reader.buffered_bytes() == 4096);
  CHECK(!reader.Receive(a[0], 99));  // duplicate cannot postpone expiry
  reader.Expire(100);
  CHECK(reader.assemblies() == 0 && reader.buffered_bytes() == 0);
  CHECK(!reader.Receive(c[0], 101));
  CHECK(reader.assemblies() == 1);
}
void RejectMalformedAndConflictingFragments() {
  auto packets = Fragments(Payload(2000), 256);
  apptraverse::P2pMessageReader reader;
  for (auto const& packet : packets) {
    for (std::size_t size = 0; size < packet.size(); ++size) {
      CHECK(!reader.Receive(Bytes{packet.begin(), packet.begin() + size}, 0));
    }
  }
  auto bad = packets.front();
  bad.push_back(0);
  CHECK(!reader.Receive(bad, 0));
  apptraverse::P2pMessageFragment fragment;
  auto copy = packets.front();
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{buffer};
  CHECK(archive.Load(fragment).IsOk());
  fragment.message_size = 0xffffffffU;
  CHECK(!reader.Receive(Pack(fragment), 0));
  fragment.message_size = 2000;
  fragment.index = 0xffffffffU;
  CHECK(!reader.Receive(Pack(fragment), 0));
  fragment.index = 0;
  fragment.payload.front() ^= 1;
  CHECK(!reader.Receive(Pack(fragment), 0));
  for (std::size_t i = 1; i < packets.size(); ++i)
    CHECK(!reader.Receive(packets[i], 0));
  CHECK(reader.buffered_bytes() == 0 && reader.malformed() != 0);
}
void ChangingMtuRestartsLayoutAndPeerRestartReassembles() {
  auto original = Payload(8192);
  apptraverse::P2pMessageWriter writer{original};
  apptraverse::P2pMessageReader reader;
  CHECK(!reader.Receive(writer.Next(1022), 0));
  writer.Advance();
  // The new first fragment must use offset zero with the smaller layout.
  std::optional<Bytes> result;
  while (!writer.done()) {
    auto packet = writer.Next(256);
    CHECK(!packet.empty() && packet.size() <= 256);
    result = reader.Receive(packet, 1);
    writer.Advance();
  }
  CHECK(result && *result == original);
  reader.Reset();  // runtime receiver loss does not lose durable sender bytes
  for (auto const& packet : Fragments(original, 256))
    result = reader.Receive(packet, 2);
  CHECK(result && *result == original);
  reader.Reset();
  for (auto const& packet : Fragments(original, 100000)) {
    CHECK(packet.size() <= 65535);
    result = reader.Receive(packet, 3);
  }
  CHECK(result && *result == original);
}
}  // namespace
int main() {
  LossReorderDuplicatesAndLostBusinessAck();
  BoundIncompleteAssembliesAndExpireWithoutTraffic();
  RejectMalformedAndConflictingFragments();
  ChangingMtuRestartsLayoutAndPeerRestartReassembles();
  std::cout << "PASS: datagram messages, 1 MiB, loss, reorder, duplicates, ACK "
               "loss, bounds, expiry, corruption, MTU and restart\n";
}
