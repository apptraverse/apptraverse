#include "apptraverse/aether_link.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <utility>

#include "aether-miscpp/format/format.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_stream_write_budget.h"
#include "aether/connection_manager/peer_presence_query.h"
#include "apptraverse/p2p_message_fragments.h"

namespace apptraverse {
namespace {

constexpr std::size_t kMaxPayload = 1024 * 1024;

std::uint64_t AddSaturated(std::uint64_t a, std::uint64_t b) {
  return b > std::numeric_limits<std::uint64_t>::max() - a
             ? std::numeric_limits<std::uint64_t>::max()
             : a + b;
}

bool ParseUid(std::string const& text, ae::Uid& uid) {
  if (text.size() != 36) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    char const c = text[i];
    bool const dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? c != '-'
             : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  uid = ae::Uid::FromString(ae::UidString{std::string_view{text}});
  return !uid.empty();
}

std::string UidText(ae::Uid const& uid) { return ae::Format("{}", uid); }

}  // namespace

void AetherPresenceSnapshot::Observe(
    ae::Result<ae::PeerPresence, int> const& result,
    std::uint64_t query_started_us) {
  if (!result) {
    Forget();
    return;
  }
  state_ = result.value().state;
  query_started_us_ = query_started_us;
}

void AetherPresenceSnapshot::Forget() {
  state_ = ae::PeerPresenceState::kUnknown;
  query_started_us_.reset();
}

LinkAvailability AetherPresenceSnapshot::Availability(
    bool locally_online, std::uint64_t now_us, std::uint64_t ttl_us) const {
  if (!locally_online) {
    return LinkAvailability::Offline;
  }
  if (!query_started_us_ || now_us < *query_started_us_ ||
      now_us - *query_started_us_ > ttl_us) {
    return LinkAvailability::Unknown;
  }
  switch (state_) {
    case ae::PeerPresenceState::kOnline:
      return LinkAvailability::Online;
    case ae::PeerPresenceState::kOffline:
      return LinkAvailability::Offline;
    case ae::PeerPresenceState::kUnknown:
      return LinkAvailability::Unknown;
  }
  return LinkAvailability::Unknown;
}

struct AetherByteLink::Impl {
  struct Peer {
    Peer(ae::Uid value, std::size_t max_message_bytes)
        : uid{value},
          packets{P2pMessageReader::Limits{.max_message_bytes =
                                               max_message_bytes}} {}
    ae::Uid uid;
    AetherPresenceSnapshot presence;
    std::uint64_t next_query_us{0};
    bool clock_started{false};
    std::uint64_t generation{0};
    std::uint64_t session_started_us{0};
    std::uint64_t last_write_progress_us{0};
    bool reset_needed{false};
    std::unique_ptr<ae::P2pStream> raw;
    P2pMessageReader packets;
    ae::Subscription chunk_sub;
    ae::Subscription write_sub;
    std::deque<P2pMessageWriter> jobs;
    bool writing{false};
    std::optional<ae::WriteAction::Status> terminal;
    AetherLinkStatistics statistics;

    void Drop() {
      write_sub.Reset();
      chunk_sub.Reset();
      writing = false;
      terminal.reset();
      jobs.clear();
      raw.reset();
      packets.Reset();
    }
    ~Peer() { Drop(); }
  };

  Impl(ae::AeContext const& ctx, ae::Client::ptr value, AetherLinkOptions opts)
      : context{ctx}, client{std::move(value)}, options{opts} {
    assert(client.is_valid() && client.is_loaded());
    assert(
        (!options.use_presence || (options.presence_query_interval_us != 0 &&
                                   options.presence_result_ttl_us != 0 &&
                                   options.presence_query_timeout_us != 0)) &&
        options.recreate_interval_us != 0 && options.max_message_bytes != 0 &&
        options.max_message_bytes <= kMaxPayload);
    endpoint = UidText(client->uid());
    port_sub = client->message_stream_manager().new_port_event().Subscribe(
        [this](ae::P2pPortHandle& handle) {
          if (stopped || !handle || handle.destination() == client->uid()) {
            return;
          }
          auto* peer = Get(handle.destination());
          Bind(*peer, std::move(handle));
        });
  }
  ~Impl() {
    stopped = true;
    receiver = {};
    query_sub.Reset();
    port_sub.Reset();
    peers.clear();
    client.Reset();
  }

  Peer* Get(ae::Uid const& uid) {
    auto const key = UidText(uid);
    auto found = peers.find(key);
    if (found == peers.end()) {
      auto peer = std::make_unique<Peer>(uid, options.max_message_bytes);
      peer->clock_started = serviced;
      found = peers.emplace(key, std::move(peer)).first;
    }
    return found->second.get();
  }
  Peer* Watch(std::string const& text) {
    ae::Uid uid;
    if (!ParseUid(text, uid) || uid == client->uid()) {
      return nullptr;
    }
    auto* peer = Get(uid);
    if (!peer->raw) {
      Bind(*peer, client->message_stream_manager().CreatePort(uid));
    }
    return peer;
  }
  LinkAvailability Availability(Peer const& peer) const {
    if (!options.use_presence) {
      return LinkAvailability::Unknown;
    }
    return peer.presence.Availability(client->IsLocallyOnline(), now_us,
                                      options.presence_result_ttl_us);
  }
  void CapturePresenceDiagnostics() {
    auto& peer = query_peer->statistics;
    peer.presence_cloud_servers = query_action->peer_cloud_server_ids().size();
    peer.presence_queried_servers = query_action->queried_server_ids().size();
    auto const& samples = query_action->samples();
    peer.presence_timing_samples = static_cast<std::size_t>(
        std::count_if(samples.begin(), samples.end(),
                      [](auto const& sample) { return sample.has_timing; }));
    statistics.presence_cloud_servers = peer.presence_cloud_servers;
    statistics.presence_queried_servers = peer.presence_queried_servers;
    statistics.presence_timing_samples = peer.presence_timing_samples;
  }
  void QueryPresence() {
    if (query_peer != nullptr) {
      bool const timed_out =
          now_us >= query_started_us &&
          now_us - query_started_us >= options.presence_query_timeout_us;
      if (!query_done && !query_action->is_finished()) {
        if (!timed_out) {
          return;
        }
        if (!query_timed_out) {
          CapturePresenceDiagnostics();
          query_peer->presence.Forget();
          ++statistics.presence_timeouts;
          ++query_peer->statistics.presence_timeouts;
          query_timed_out = true;
        }
        // Keep this subscription and its original start time until the action
        // finishes or a different due UID replaces it below. Querying the same
        // UID would return this unfinished action, not start a fresh request.
      } else {
        if (!query_done) {
          query_peer->presence.Forget();
          // The SDK has no result replay for synchronous completion.
          ++statistics.presence_errors;
          ++query_peer->statistics.presence_errors;
        }
        // Complete emits result_event before Finish. Replacing the Client-owned
        // action is safe here on the next Service call, outside that callback.
        query_sub.Reset();
        query_peer = nullptr;
        query_action = nullptr;
      }
    }
    if (peers.empty() || !client->IsLocallyOnline()) {
      return;
    }
    auto next = peers.upper_bound(last_queried);
    if (next == peers.end()) {
      next = peers.begin();
    }
    for (std::size_t count = 0; count < peers.size(); ++count) {
      auto& peer = *next->second;
      if (&peer != query_peer && now_us >= peer.next_query_us) {
        query_sub.Reset();
        last_queried = next->first;
        query_peer = &peer;
        query_started_us = now_us;
        query_done = false;
        query_timed_out = false;
        peer.next_query_us =
            AddSaturated(now_us, options.presence_query_interval_us);
        ++statistics.presence_queries;
        ++peer.statistics.presence_queries;
        // A different UID replaces/cancels the old Client-owned query.
        query_action = &client->QueryPeerPresence(peer.uid);
        query_sub =
            query_action->result_event().Subscribe([this](auto const& result) {
              if (stopped || query_peer == nullptr) {
                return;
              }
              CapturePresenceDiagnostics();
              query_peer->presence.Observe(result, query_started_us);
              if (!result) {
                ++statistics.presence_errors;
                ++query_peer->statistics.presence_errors;
                statistics.last_presence_error = result.error();
                query_peer->statistics.last_presence_error = result.error();
              } else {
                switch (result.value().state) {
                  case ae::PeerPresenceState::kOnline:
                    ++statistics.presence_online_results;
                    ++query_peer->statistics.presence_online_results;
                    break;
                  case ae::PeerPresenceState::kOffline:
                    ++statistics.presence_offline_results;
                    ++query_peer->statistics.presence_offline_results;
                    break;
                  case ae::PeerPresenceState::kUnknown:
                    ++statistics.presence_unknown_results;
                    ++query_peer->statistics.presence_unknown_results;
                    break;
                }
              }
              query_done = true;
            });
        return;  // No catch-up requests or concurrent Client-owned actions.
      }
      if (++next == peers.end()) {
        next = peers.begin();
      }
    }
  }
  void Bind(Peer& peer, ae::P2pPortHandle handle) {
    bool const replacement = peer.generation != 0;
    peer.Drop();
    if (replacement) {
      ++statistics.session_replacements;
      ++peer.statistics.session_replacements;
    }
    ++peer.generation;
    peer.session_started_us = now_us;
    peer.last_write_progress_us = now_us;
    peer.reset_needed = false;
    peer.raw = std::make_unique<ae::P2pStream>(context, client.Load(), peer.uid,
                                               std::move(handle));
    auto* const raw_peer = &peer;
    auto const generation = peer.generation;
    peer.chunk_sub = peer.raw->out_data_event().Subscribe(
        [this, raw_peer, generation](ae::DataBuffer const& data) {
          if (!stopped && raw_peer->generation == generation) {
            auto const malformed_before = raw_peer->packets.malformed();
            auto message = raw_peer->packets.Receive(data, now_us);
            auto const malformed =
                raw_peer->packets.malformed() - malformed_before;
            statistics.malformed_messages += malformed;
            raw_peer->statistics.malformed_messages += malformed;
            if (message) Receive(*raw_peer, *message);
          }
        });
  }
  bool Queue(Peer& peer, std::vector<std::uint8_t> const& bytes) {
    if (!peer.jobs.empty()) {
      return false;
    }
    peer.jobs.emplace_back(bytes);
    return true;
  }
  void Receive(Peer& peer, ae::DataBuffer const& data) {
    if (data.empty() || data.size() > options.max_message_bytes) {
      ++statistics.malformed_messages;
      ++peer.statistics.malformed_messages;
      return;
    }
    ++statistics.data_received;
    ++peer.statistics.data_received;
    if (receiver) {
      receiver(UidText(peer.uid), data);
    }
  }
  void Pump(Peer& peer) {
    if (peer.terminal) {
      auto const status = *peer.terminal;
      peer.write_sub.Reset();
      peer.terminal.reset();
      peer.writing = false;
      if (status != ae::WriteAction::Status::kSuccess) {
        ++statistics.write_failures;
        ++peer.statistics.write_failures;
        peer.reset_needed = true;
      } else if (!peer.jobs.empty()) {
        peer.last_write_progress_us = now_us;
        auto& job = peer.jobs.front();
        job.Advance();
        if (job.done()) {
          ++statistics.data_completed;
          ++peer.statistics.data_completed;
          peer.jobs.pop_front();
        }
      }
    }
    auto const availability = Availability(peer);
    auto const stalled =
        now_us >= peer.last_write_progress_us &&
        now_us - peer.last_write_progress_us >= options.recreate_interval_us;
    if ((peer.reset_needed ||
         ((!options.use_presence || !options.gate_delivery_on_presence ||
           availability == LinkAvailability::Offline) &&
          stalled && !peer.jobs.empty())) &&
        now_us >= peer.session_started_us &&
        now_us - peer.session_started_us >= options.recreate_interval_us) {
      // SharedNode retains the pending business message for durable retry.
      Bind(peer, client->message_stream_manager().CreatePort(peer.uid));
    }
    peer.packets.Expire(now_us);
    if (peer.reset_needed || peer.writing || !peer.raw ||
        (options.use_presence && options.gate_delivery_on_presence &&
         availability != LinkAvailability::Online)) {
      return;
    }
    auto const info = peer.raw->stream_info();
    if (peer.jobs.empty() || !info.is_writable || info.max_element_size == 0) {
      return;
    }
    auto& job = peer.jobs.front();
    auto const budget = std::min<std::size_t>(
        65535,
        ae::P2pStreamWritePayloadLimitFromChannelMax(info.max_element_size));
    auto chunk = job.Next(budget);
    if (chunk.empty()) {
      peer.reset_needed = true;
      return;
    }
    peer.writing = true;
    auto const generation = peer.generation;
    auto* const raw_peer = &peer;
    auto& action = peer.raw->Write(std::move(chunk));
    if (action.is_finished()) {
      // The SDK provides no readable status after synchronous completion.
      peer.terminal = ae::WriteAction::Status::kFail;
      return;
    }
    peer.write_sub = action.status_event().Subscribe(
        [this, raw_peer, generation](ae::WriteAction::Status status) {
          if (!stopped && raw_peer->generation == generation) {
            raw_peer->terminal = status;
          }
        });
  }

  ae::AeContext context;
  ae::Client::ptr client;
  AetherLinkOptions options;
  std::string endpoint;
  IByteLink::Receiver receiver;
  std::map<std::string, std::unique_ptr<Peer>> peers;
  ae::Subscription port_sub;
  ae::Subscription query_sub;
  Peer* query_peer{nullptr};
  ae::PeerPresenceQuery* query_action{nullptr};
  std::string last_queried;
  std::uint64_t query_started_us{0};
  bool query_done{false};
  bool query_timed_out{false};
  AetherLinkStatistics statistics;
  std::uint64_t now_us{0};
  bool serviced{false};
  bool stopped{false};
};

AetherByteLink::AetherByteLink(ae::AeContext const& context,
                               ae::Client::ptr client,
                               AetherLinkOptions options)
    : impl_{std::make_unique<Impl>(context, std::move(client), options)} {}
AetherByteLink::~AetherByteLink() = default;
std::string const& AetherByteLink::EndpointId() const {
  return impl_->endpoint;
}
bool AetherByteLink::WatchPeer(std::string const& endpoint) {
  return impl_->Watch(endpoint) != nullptr;
}
void AetherByteLink::Service(std::uint64_t now_us) {
  assert(!impl_->serviced || now_us >= impl_->now_us);
  impl_->now_us = now_us;
  if (impl_->options.use_presence) {
    impl_->QueryPresence();
  }
  for (auto const& [key, peer] : impl_->peers) {
    if (!peer->clock_started) {
      peer->clock_started = true;
      peer->session_started_us = now_us;
      peer->last_write_progress_us = now_us;
    }
    impl_->Pump(*peer);
  }
  impl_->serviced = true;
}
void AetherByteLink::SetReceiver(Receiver receiver) {
  impl_->receiver = std::move(receiver);
}
bool AetherByteLink::Send(std::string const& destination,
                          std::vector<std::uint8_t> const& bytes) {
  if (bytes.empty() || bytes.size() > impl_->options.max_message_bytes) {
    return false;
  }
  auto* peer = impl_->Watch(destination);
  if (peer == nullptr || peer->reset_needed ||
      (impl_->options.use_presence &&
       impl_->options.gate_delivery_on_presence &&
       impl_->Availability(*peer) != LinkAvailability::Online) ||
      !impl_->Queue(*peer, bytes)) {
    return false;
  }
  // Retain one frame even while the SDK is unwritable. Pump can then detect
  // a stalled queued write and recreate its session; refusing before Queue
  // leaves an empty, unwritable session outside that recovery path forever.
  ++impl_->statistics.data_accepted;
  ++peer->statistics.data_accepted;
  return true;
}
LinkAvailability AetherByteLink::Availability(
    std::string const& destination) const {
  if (impl_->options.use_presence &&
      !impl_->options.gate_delivery_on_presence) {
    ae::Uid uid;
    return ParseUid(destination, uid) && uid != impl_->client->uid()
               ? LinkAvailability::Unknown
               : LinkAvailability::Offline;
  }
  return ObservedPresence(destination);
}
LinkAvailability AetherByteLink::ObservedPresence(
    std::string const& destination) const {
  ae::Uid uid;
  if (!ParseUid(destination, uid) || uid == impl_->client->uid()) {
    return LinkAvailability::Offline;
  }
  if (!impl_->options.use_presence) {
    return LinkAvailability::Unknown;
  }
  if (!impl_->client->IsLocallyOnline()) {
    return LinkAvailability::Offline;
  }
  auto const found = impl_->peers.find(UidText(uid));
  return found == impl_->peers.end() ? LinkAvailability::Unknown
                                     : impl_->Availability(*found->second);
}
AetherLinkStatistics const& AetherByteLink::stats() const {
  return impl_->statistics;
}
AetherLinkStatistics AetherByteLink::PeerStats(
    std::string const& destination) const {
  ae::Uid uid;
  if (!ParseUid(destination, uid)) {
    return {};
  }
  auto const found = impl_->peers.find(UidText(uid));
  if (found == impl_->peers.end()) {
    return {};
  }
  auto statistics = found->second->statistics;
  if (found->second->raw) {
    auto const path = found->second->raw->InspectSendPathTransport();
    statistics.linked_tcp_endpoint = path.linked_tcp_endpoint;
    statistics.linked_udp_endpoint = path.linked_udp_endpoint;
    statistics.recommended_element_size = path.aggregated.rec_element_size;
    statistics.maximum_element_size = path.aggregated.max_element_size;
    statistics.writable = path.aggregated.is_writable;
  }
  return statistics;
}

}  // namespace apptraverse
