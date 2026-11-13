#include "apptraverse/p2p_message_fragments.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

#include "aether-miscpp/serialization/binary_archive.h"

namespace apptraverse {
namespace {
constexpr std::size_t kHeaderBudget = 64;
constexpr std::size_t kMinFragment = 64;

std::pair<std::uint64_t, std::uint64_t> Checksum(
    std::vector<std::uint8_t> const& bytes) {
  std::uint64_t a = 14695981039346656037ULL;
  std::uint64_t b = 1099511628211ULL;
  for (auto byte : bytes) {
    a = (a ^ byte) * 1099511628211ULL;
    b ^= byte + 0x9e3779b97f4a7c15ULL + (b << 6) + (b >> 2);
  }
  return {a, b};
}
}  // namespace

P2pMessageWriter::P2pMessageWriter(std::vector<std::uint8_t> bytes)
    : bytes_{std::move(bytes)} {
  auto const checksum = Checksum(bytes_);
  checksum_a_ = checksum.first;
  checksum_b_ = checksum.second;
}

std::vector<std::uint8_t> P2pMessageWriter::Next(std::size_t budget) {
  budget = std::min<std::size_t>(budget, 65535);
  if (bytes_.empty() ||
      bytes_.size() > std::numeric_limits<std::uint32_t>::max() ||
      budget < kHeaderBudget + kMinFragment) {
    return {};
  }
  auto const capacity = std::min<std::size_t>(budget - kHeaderBudget, 65535);
  if (fragment_size_ == 0 || capacity < fragment_size_) {
    fragment_size_ = capacity;
    index_ = 0;
  }
  if (done()) {
    return {};
  }
  auto const offset = index_ * fragment_size_;
  auto const length = std::min(fragment_size_, bytes_.size() - offset);
  P2pMessageFragment fragment;
  fragment.checksum_a = checksum_a_;
  fragment.checksum_b = checksum_b_;
  fragment.message_size = static_cast<std::uint32_t>(bytes_.size());
  fragment.fragment_size = static_cast<std::uint32_t>(fragment_size_);
  fragment.index = static_cast<std::uint32_t>(index_);
  fragment.payload.assign(
      bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
      bytes_.begin() + static_cast<std::ptrdiff_t>(offset + length));
  std::vector<std::uint8_t> wire;
  ae::seri::BinaryVectorBuffer buffer{wire};
  ae::seri::BinaryArchive archive{buffer};
  if (archive.Save(fragment).IsErr() || wire.size() > budget) {
    return {};
  }
  return wire;
}

void P2pMessageWriter::Advance() {
  assert(fragment_size_ != 0 && !done());
  ++index_;
}
bool P2pMessageWriter::done() const {
  return bytes_.empty() || (fragment_size_ != 0 &&
                            index_ >= (bytes_.size() - 1) / fragment_size_ + 1);
}

P2pMessageReader::P2pMessageReader() : P2pMessageReader{Limits{}} {}
P2pMessageReader::P2pMessageReader(Limits limits) : limits_{limits} {
  assert(limits.max_message_bytes != 0 && limits.max_assemblies != 0 &&
         limits.max_buffered_bytes >= limits.max_message_bytes &&
         limits.idle_timeout_us != 0);
}

std::optional<std::vector<std::uint8_t>> P2pMessageReader::Receive(
    std::vector<std::uint8_t> const& datagram, std::uint64_t now_us) {
  Expire(now_us);
  if (datagram.size() > 65535) {
    ++malformed_;
    return {};
  }
  P2pMessageFragment fragment;
  auto copy = datagram;
  ae::seri::BinaryVectorBuffer reader{copy};
  ae::seri::BinaryArchive archive{reader, datagram.size()};
  if (archive.Load(fragment).IsErr() ||
      archive.buffer().read_position() != datagram.size() ||
      fragment.version != 1 || fragment.message_size == 0 ||
      fragment.message_size > limits_.max_message_bytes ||
      fragment.fragment_size < kMinFragment || fragment.fragment_size > 65535 ||
      fragment.index > (fragment.message_size - 1) / fragment.fragment_size) {
    ++malformed_;
    return {};
  }
  auto const offset =
      static_cast<std::size_t>(fragment.index) * fragment.fragment_size;
  auto const count = std::min<std::size_t>(fragment.fragment_size,
                                           fragment.message_size - offset);
  if (fragment.payload.size() != count) {
    ++malformed_;
    return {};
  }
  Key const key{fragment.checksum_a, fragment.checksum_b, fragment.message_size,
                fragment.fragment_size};
  auto found = assemblies_.find(key);
  if (found == assemblies_.end()) {
    if (assemblies_.size() >= limits_.max_assemblies ||
        fragment.message_size > limits_.max_buffered_bytes - buffered_bytes_) {
      return {};  // Bounded admission; durable SharedSync will retry later.
    }
    Assembly assembly;
    assembly.bytes.resize(fragment.message_size);
    assembly.missing = (fragment.message_size - 1) / fragment.fragment_size + 1;
    assembly.received.resize(assembly.missing);
    assembly.last_progress_us = now_us;
    buffered_bytes_ += assembly.bytes.size();
    found = assemblies_.emplace(key, std::move(assembly)).first;
  }
  auto& assembly = found->second;
  if (assembly.received[fragment.index]) {
    if (!std::equal(
            fragment.payload.begin(), fragment.payload.end(),
            assembly.bytes.begin() + static_cast<std::ptrdiff_t>(offset))) {
      ++malformed_;
    }
    return {};  // Duplicate traffic cannot keep an incomplete assembly alive.
  }
  std::copy(fragment.payload.begin(), fragment.payload.end(),
            assembly.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
  assembly.received[fragment.index] = true;
  assembly.last_progress_us = now_us;
  if (--assembly.missing != 0) {
    return {};
  }
  auto bytes = std::move(assembly.bytes);
  buffered_bytes_ -= bytes.size();
  assemblies_.erase(found);
  auto const checksum = Checksum(bytes);
  if (checksum.first != fragment.checksum_a ||
      checksum.second != fragment.checksum_b) {
    ++malformed_;
    return {};
  }
  // Redelivery is intentional: if a business ACK was lost, SharedSync must see
  // the retry and send its durable ACK again, without applying the Event twice.
  return bytes;
}

void P2pMessageReader::Expire(std::uint64_t now_us) {
  for (auto it = assemblies_.begin(); it != assemblies_.end();) {
    auto const last = it->second.last_progress_us;
    if (now_us >= last && now_us - last >= limits_.idle_timeout_us) {
      buffered_bytes_ -= it->second.bytes.size();
      it = assemblies_.erase(it);
    } else {
      ++it;
    }
  }
}
void P2pMessageReader::Reset() {
  assemblies_.clear();
  buffered_bytes_ = 0;
}
}  // namespace apptraverse
