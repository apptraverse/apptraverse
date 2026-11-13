#ifndef APPTRAVERSE_AETHER_LINK_H_
#define APPTRAVERSE_AETHER_LINK_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "aether-miscpp/types/result.h"
#include "aether/ae_context.h"
#include "aether/client.h"
#include "aether/remote_presence.h"
#include "apptraverse/memory_link.h"

namespace apptraverse {

struct AetherLinkOptions {
  // Disabled by default: send without availability observations and let the
  // durable SharedSync ACK/retry loop establish delivery.
  bool use_presence{false};
  std::uint64_t presence_query_interval_us{1000000};
  std::uint64_t presence_result_ttl_us{5000000};
  std::uint64_t presence_query_timeout_us{5000000};
  std::uint64_t recreate_interval_us{5000000};
  // SDK PacketSize supplies framing; SharedSync supplies native serialization.
  std::size_t max_message_bytes{1024 * 1024};
};

// Cache of the native Client::QueryPeerPresence result, with no presence
// inference from data, ACKs, request errors or elapsed time. Expiry is Unknown.
// Query start time bounds freshness even if the native result arrives late.
class AetherPresenceSnapshot {
 public:
  void Observe(ae::Result<ae::PeerPresence, int> const& result,
               std::uint64_t query_started_us);
  void Forget();
  LinkAvailability Availability(bool locally_online, std::uint64_t now_us,
                                std::uint64_t ttl_us) const;

 private:
  ae::PeerPresenceState state_{ae::PeerPresenceState::kUnknown};
  std::optional<std::uint64_t> query_started_us_;
};

struct AetherLinkStatistics {
  std::uint64_t data_accepted{0};
  std::uint64_t data_received{0};
  std::uint64_t data_completed{0};
  std::uint64_t presence_queries{0};
  std::uint64_t presence_errors{0};
  std::uint64_t presence_timeouts{0};
  std::uint64_t presence_online_results{0};
  std::uint64_t presence_offline_results{0};
  std::uint64_t presence_unknown_results{0};
  int last_presence_error{0};
  std::size_t presence_cloud_servers{0};
  std::size_t presence_queried_servers{0};
  std::size_t presence_timing_samples{0};
  std::uint64_t malformed_messages{0};
  std::uint64_t write_failures{0};
  std::uint64_t session_replacements{0};
};

// Model-thread adapter. The context, Client and its AetherApp must outlive it.
// Owns this Client's peer-port subscription. When use_presence is enabled it
// also owns QueryPeerPresence; another consumer must not replace that action.
// Multiple SharedNodes share this one byte link. Service uses one monotonic
// local timeline; it does not update AetherApp. With presence disabled, valid
// destinations report Unknown and sends need no Online result. The optional
// presence mode requires native local Online and a fresh remote Online result.
// One business message per peer can be queued/in flight. Failure remains the
// SharedSyncRuntime's durable retry obligation, rather than a second SDK queue.
// An unwritable SDK stream retains that one frame for stall/recovery handling.
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
