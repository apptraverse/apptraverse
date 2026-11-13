#ifndef APPTRAVERSE_P2P_MESSAGE_FRAGMENTS_H_
#define APPTRAVERSE_P2P_MESSAGE_FRAGMENTS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-miscpp/serialization/serialization.h"

namespace apptraverse {

// Runtime envelope only. The native SharedSync archive is never changed. A
// content key is stable across durable retries, so fragments from different
// attempts can fill the same assembly. This checksum is not authentication;
// authentication is supplied by the underlying Aether connection.
struct P2pMessageFragment {
  std::uint16_t version{1};
  std::uint64_t checksum_a{};
  std::uint64_t checksum_b{};
  std::uint32_t message_size{};
  std::uint32_t fragment_size{};
  std::uint32_t index{};
  std::vector<std::uint8_t> payload;
  AE_REFLECT_MEMBERS(version, checksum_a, checksum_b, message_size,
                     fragment_size, index, payload)
};

class P2pMessageWriter {
 public:
  explicit P2pMessageWriter(std::vector<std::uint8_t> bytes);
  // The caller accounts for SDK framing/crypto in the P2pStream payload budget.
  // A smaller path budget restarts fragmentation with a new layout. Advance
  // only after local handoff; the durable caller retains the delivery duty.
  std::vector<std::uint8_t> Next(std::size_t datagram_budget);
  void Advance();
  bool done() const;

 private:
  std::vector<std::uint8_t> bytes_;
  std::uint64_t checksum_a_{}, checksum_b_{};
  std::size_t fragment_size_{0}, index_{0};
};

class P2pMessageReader {
 public:
  struct Limits {
    std::size_t max_message_bytes{1024 * 1024};
    std::size_t max_assemblies{16};
    std::size_t max_buffered_bytes{8 * 1024 * 1024};
    std::uint64_t idle_timeout_us{30000000};
  };
  P2pMessageReader();
  explicit P2pMessageReader(Limits limits);
  std::optional<std::vector<std::uint8_t>> Receive(
      std::vector<std::uint8_t> const& datagram, std::uint64_t now_us);
  void Expire(std::uint64_t now_us);
  void Reset();
  std::size_t buffered_bytes() const { return buffered_bytes_; }
  std::size_t assemblies() const { return assemblies_.size(); }
  std::uint64_t malformed() const { return malformed_; }

 private:
  using Key =
      std::tuple<std::uint64_t, std::uint64_t, std::uint32_t, std::uint32_t>;
  struct Assembly {
    std::vector<std::uint8_t> bytes;
    std::vector<bool> received;
    std::size_t missing{0};
    std::uint64_t last_progress_us{0};
  };
  Limits limits_;
  std::map<Key, Assembly> assemblies_;
  std::size_t buffered_bytes_{0};
  std::uint64_t malformed_{0};
};

}  // namespace apptraverse
#endif
