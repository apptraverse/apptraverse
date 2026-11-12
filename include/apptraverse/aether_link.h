#ifndef APPTRAVERSE_AETHER_LINK_H_
#define APPTRAVERSE_AETHER_LINK_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aether/ae_context.h"
#include "aether/client.h"
#include "aether-miscpp/reflect/reflect.h"
#include "apptraverse/memory_link.h"

namespace apptraverse {

struct AetherLinkOptions {
  std::uint64_t probe_interval_us{1000000};
  std::uint64_t presence_timeout_us{5000000};
  std::uint64_t recreate_interval_us{5000000};
  // SDK PacketSize frames at most 1049834 bytes, including this envelope.
  std::size_t max_message_bytes{1024 * 1024};
};

enum class AetherLinkKind : std::uint8_t { Data = 1, Probe = 2, Reply = 3 };

// Native transport control, independent of SharedNode's native sync message.
// Message boundaries and fragmentation are supplied by SDK stream gates.
struct AetherLinkEnvelope {
  std::uint16_t version{1};
  AetherLinkKind kind{AetherLinkKind::Data};
  std::uint64_t sequence{0};
  std::uint64_t request_time_us{0};
  std::vector<std::uint8_t> payload;
  AE_REFLECT_MEMBERS(version, kind, sequence, request_time_us, payload)
};

bool SerializeAetherLinkEnvelope(AetherLinkEnvelope const& envelope,
                                  std::vector<std::uint8_t>& bytes,
                                  std::size_t max_payload_bytes = 1024 * 1024);
bool DeserializeAetherLinkEnvelope(std::vector<std::uint8_t> const& bytes,
                                    AetherLinkEnvelope& envelope,
                                    std::size_t max_payload_bytes = 1024 * 1024);

// Small deterministic seam shared by the real adapter and headless tests.
// Only an outstanding, exact, unexpired echoed request proves Online. Data,
// native push ACKs, and late buffered replies do not establish freshness.
class AetherPeerPresence {
 public:
  explicit AetherPeerPresence(std::uint64_t watched_at_us = 0,
                              std::uint64_t interval_us = 1000000,
                              std::uint64_t timeout_us = 5000000,
                              std::uint64_t initial_sequence = 0);
  std::optional<AetherLinkEnvelope> NextProbe(std::uint64_t now_us);
  bool ObserveReply(AetherLinkEnvelope const& reply, std::uint64_t now_us);
  LinkAvailability Availability(std::uint64_t now_us) const;
  void ResetOutstanding(std::uint64_t now_us);
  std::size_t outstanding_count() const { return outstanding_.size(); }

 private:
  std::uint64_t watched_at_us_;
  std::uint64_t interval_us_;
  std::uint64_t timeout_us_;
  std::uint64_t next_probe_us_;
  std::uint64_t sequence_{0};
  std::optional<std::uint64_t> latest_fresh_request_us_;
  std::deque<std::pair<std::uint64_t, std::uint64_t>> outstanding_;
};

struct AetherLinkStatistics {
  std::uint64_t data_accepted{0};
  std::uint64_t data_received{0};
  std::uint64_t data_completed{0};
  std::uint64_t probes_sent{0};
  std::uint64_t replies_received{0};
  std::uint64_t stale_replies{0};
  std::uint64_t malformed_messages{0};
  std::uint64_t write_failures{0};
  std::uint64_t session_replacements{0};
};

// Model-thread adapter. The context, Client and its AetherApp must outlive it.
// Owns this Client's peer-port subscription; do not co-own its receive ports
// with another P2P consumer. Multiple SharedNodes share this one byte link.
// Service uses one monotonic local timeline; it does not update AetherApp.
// One business message per peer can be queued/in flight. Failure remains the
// SharedSyncRuntime's durable retry obligation, rather than a second SDK queue.
class AetherByteLink final : public IByteLink {
 public:
  AetherByteLink(ae::AeContext const& context, ae::Client::ptr client,
                  AetherLinkOptions options = {});
  ~AetherByteLink() override;
  AetherByteLink(AetherByteLink const&) = delete;
  AetherByteLink& operator=(AetherByteLink const&) = delete;

  std::string const& EndpointId() const;
  bool WatchPeer(std::string const& endpoint);
  void Service(std::uint64_t now_us);
  void SetReceiver(Receiver receiver) override;
  bool Send(std::string const& destination,
            std::vector<std::uint8_t> const& bytes) override;
  LinkAvailability Availability(std::string const& destination) const override;
  AetherLinkStatistics const& stats() const;
  AetherLinkStatistics PeerStats(std::string const& destination) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_AETHER_LINK_H_
