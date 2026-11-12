#include "apptraverse/aether_link.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <limits>
#include <map>
#include <utility>

#include "aether-miscpp/format/format.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/stream_api/sized_packet_gate.h"

namespace apptraverse {
namespace {

constexpr std::size_t kMaxPayload = 1024 * 1024;
constexpr std::size_t kEnvelopeAllowance = 128;

std::uint64_t AddSaturated(std::uint64_t a, std::uint64_t b) {
  return b > std::numeric_limits<std::uint64_t>::max() - a
      ? std::numeric_limits<std::uint64_t>::max() : a + b;
}

bool ValidEnvelope(AetherLinkEnvelope const& envelope, std::size_t max_payload) {
  if (envelope.version != 1 || envelope.payload.size() > max_payload) { return false; }
  switch (envelope.kind) {
    case AetherLinkKind::Data:
      return envelope.sequence == 0 && envelope.request_time_us == 0 &&
             !envelope.payload.empty();
    case AetherLinkKind::Probe:
    case AetherLinkKind::Reply:
      return envelope.sequence != 0 && envelope.payload.empty();
  }
  return false;
}

bool ParseUid(std::string const& text, ae::Uid& uid) {
  if (text.size() != 36) { return false; }
  for (std::size_t i = 0; i < text.size(); ++i) {
    char const c = text[i];
    bool const dash = i == 8 || i == 13 || i == 18 || i == 23;
    if (dash ? c != '-' : !((c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  uid = ae::Uid::FromString(ae::UidString{std::string_view{text}});
  return !uid.empty();
}

std::string UidText(ae::Uid const& uid) { return ae::Format("{}", uid); }

}  // namespace

bool SerializeAetherLinkEnvelope(AetherLinkEnvelope const& envelope,
    std::vector<std::uint8_t>& bytes, std::size_t max_payload_bytes) {
  if (max_payload_bytes > kMaxPayload || !ValidEnvelope(envelope, max_payload_bytes)) {
    return false;
  }
  std::vector<std::uint8_t> output;
  ae::seri::BinaryVectorBuffer buffer{output};
  ae::seri::BinaryArchive archive{buffer};
  if (archive.Save(envelope).IsErr()) { return false; }
  bytes = std::move(output);
  return true;
}

bool DeserializeAetherLinkEnvelope(std::vector<std::uint8_t> const& bytes,
    AetherLinkEnvelope& envelope, std::size_t max_payload_bytes) {
  if (max_payload_bytes > kMaxPayload || bytes.empty() ||
      bytes.size() > max_payload_bytes + kEnvelopeAllowance) { return false; }
  auto copy = bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{buffer};
  AetherLinkEnvelope decoded;
  if (archive.Load(decoded).IsErr() || archive.buffer().read_position() != copy.size() ||
      !ValidEnvelope(decoded, max_payload_bytes)) { return false; }
  envelope = std::move(decoded);
  return true;
}

AetherPeerPresence::AetherPeerPresence(std::uint64_t watched_at_us,
    std::uint64_t interval_us, std::uint64_t timeout_us, std::uint64_t initial_sequence)
    : watched_at_us_{watched_at_us}, interval_us_{interval_us},
      timeout_us_{timeout_us}, next_probe_us_{watched_at_us}, sequence_{initial_sequence} {
  assert(interval_us_ != 0 && timeout_us_ != 0);
}

std::optional<AetherLinkEnvelope> AetherPeerPresence::NextProbe(std::uint64_t now_us) {
  if (now_us < watched_at_us_ || now_us < next_probe_us_ ||
      sequence_ == std::numeric_limits<std::uint64_t>::max()) { return {}; }
  while (!outstanding_.empty() && now_us >= outstanding_.front().second &&
         now_us - outstanding_.front().second > timeout_us_) {
    outstanding_.pop_front();
  }
  // Bound bookkeeping even when a caller chooses an unusually short cadence.
  if (outstanding_.size() == 64) { outstanding_.pop_front(); }
  AetherLinkEnvelope probe;
  probe.kind = AetherLinkKind::Probe;
  probe.sequence = ++sequence_;
  probe.request_time_us = now_us;
  outstanding_.emplace_back(probe.sequence, now_us);
  next_probe_us_ = AddSaturated(now_us, interval_us_);  // No catch-up bursts.
  return probe;
}

bool AetherPeerPresence::ObserveReply(AetherLinkEnvelope const& reply,
                                      std::uint64_t now_us) {
  if (reply.kind != AetherLinkKind::Reply || !ValidEnvelope(reply, 0)) { return false; }
  auto const found = std::find(outstanding_.begin(), outstanding_.end(),
      std::pair{reply.sequence, reply.request_time_us});
  if (found == outstanding_.end() || now_us < reply.request_time_us ||
      now_us - reply.request_time_us > timeout_us_) { return false; }
  if (!latest_fresh_request_us_ || reply.request_time_us > *latest_fresh_request_us_) {
    latest_fresh_request_us_ = reply.request_time_us;
  }
  outstanding_.erase(found);
  return true;
}

LinkAvailability AetherPeerPresence::Availability(std::uint64_t now_us) const {
  if (latest_fresh_request_us_) {
    return now_us >= *latest_fresh_request_us_ &&
           now_us - *latest_fresh_request_us_ <= timeout_us_
        ? LinkAvailability::Online : LinkAvailability::Offline;
  }
  return now_us >= watched_at_us_ && now_us - watched_at_us_ > timeout_us_
      ? LinkAvailability::Offline : LinkAvailability::Unknown;
}

void AetherPeerPresence::ResetOutstanding(std::uint64_t now_us) {
  outstanding_.clear();
  next_probe_us_ = now_us;
}

struct AetherByteLink::Impl {
  struct Job {
    AetherLinkKind kind;
    std::vector<std::uint8_t> wire;
    std::size_t offset{0};
  };
  struct Peer {
    Peer(ae::Uid value, AetherLinkOptions const& options, std::uint64_t now)
        : uid{value}, sequence_seed{static_cast<std::uint64_t>(
              std::chrono::steady_clock::now().time_since_epoch().count())},
          presence{now, options.probe_interval_us, options.presence_timeout_us,
                     sequence_seed} {}
    ae::Uid uid;
    std::uint64_t sequence_seed;
    AetherPeerPresence presence;
    bool clock_started{false};
    std::uint64_t generation{0};
    std::uint64_t session_started_us{0};
    std::uint64_t last_write_progress_us{0};
    bool reset_needed{false};
    std::shared_ptr<ae::P2pStream> raw;
    std::shared_ptr<ae::P2pSafeStream> safe;
    std::unique_ptr<ae::SizedPacketGate> packets;
    ae::Subscription packet_sub;
    ae::Subscription chunk_sub;
    ae::Subscription write_sub;
    std::deque<Job> jobs;
    bool writing{false};
    std::optional<ae::WriteAction::Status> terminal;
    std::size_t written_end{0};
    AetherLinkStatistics statistics;

    void Drop() {
      write_sub.Reset(); chunk_sub.Reset(); packet_sub.Reset();
      writing = false; terminal.reset(); jobs.clear();
      safe.reset(); raw.reset(); packets.reset();
    }
    ~Peer() { Drop(); }
  };

  Impl(ae::AeContext const& ctx, ae::Client::ptr value, AetherLinkOptions opts)
      : context{ctx}, client{std::move(value)}, options{opts} {
    assert(client.is_valid() && client.is_loaded());
    assert(options.probe_interval_us != 0 && options.presence_timeout_us != 0 &&
           options.recreate_interval_us != 0 && options.max_message_bytes != 0 &&
           options.max_message_bytes <= kMaxPayload);
    endpoint = UidText(client->uid());
    port_sub = client->message_stream_manager().new_port_event().Subscribe(
        [this](ae::P2pPortHandle& handle) {
          if (stopped || !handle || handle.destination() == client->uid()) { return; }
          auto* peer = Get(handle.destination());
          Bind(*peer, std::move(handle));
        });
  }
  ~Impl() {
    stopped = true; receiver = {}; port_sub.Reset(); peers.clear(); client.Reset();
  }

  Peer* Get(ae::Uid const& uid) {
    auto const key = UidText(uid);
    auto found = peers.find(key);
    if (found == peers.end()) {
      auto peer = std::make_unique<Peer>(uid, options, now_us);
      peer->clock_started = serviced;
      found = peers.emplace(key, std::move(peer)).first;
    }
    return found->second.get();
  }
  Peer* Watch(std::string const& text) {
    ae::Uid uid;
    if (!ParseUid(text, uid) || uid == client->uid()) { return nullptr; }
    auto* peer = Get(uid);
    if (!peer->safe) { Bind(*peer, client->message_stream_manager().CreatePort(uid)); }
    return peer;
  }
  void Bind(Peer& peer, ae::P2pPortHandle handle) {
    bool const replacement = peer.generation != 0;
    peer.Drop();
    if (replacement) { ++statistics.session_replacements; ++peer.statistics.session_replacements; }
    ++peer.generation;
    peer.session_started_us = now_us;
    peer.last_write_progress_us = now_us;
    peer.reset_needed = false;
    peer.presence.ResetOutstanding(now_us);
    peer.raw = std::make_shared<ae::P2pStream>(context, client.Load(), peer.uid,
                                              std::move(handle));
    using namespace std::chrono_literals;
    auto const config = ae::SafeStreamConfig{
        .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
        .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
        .max_repeat_count = 10, .wait_ack_timeout = 1500ms,
        .send_ack_timeout = 0ms, .send_repeat_timeout = 200ms};
    peer.safe = std::make_shared<ae::P2pSafeStream>(context, config, peer.raw);
    peer.packets = std::make_unique<ae::SizedPacketGate>(context);
    auto* const raw_peer = &peer;
    auto const generation = peer.generation;
    peer.chunk_sub = peer.safe->out_data_event().Subscribe(
        [this, raw_peer, generation](ae::DataBuffer const& data) {
          if (!stopped && raw_peer->generation == generation) {
            raw_peer->packets->WriteOut(data);
          }
        });
    peer.packet_sub = peer.packets->out_data_event().Subscribe(
        [this, raw_peer, generation](ae::DataBuffer const& data) {
          if (!stopped && raw_peer->generation == generation) { Receive(*raw_peer, data); }
        });
  }
  bool Has(Peer const& peer, AetherLinkKind kind) const {
    return std::any_of(peer.jobs.begin(), peer.jobs.end(),
        [kind](auto const& job) { return job.kind == kind; });
  }
  bool Queue(Peer& peer, AetherLinkEnvelope const& envelope) {
    if (Has(peer, envelope.kind)) { return false; }
    std::vector<std::uint8_t> bytes;
    if (!SerializeAetherLinkEnvelope(envelope, bytes, options.max_message_bytes)) { return false; }
    auto wire = peer.packets->WriteIn(ae::DataBuffer{bytes.begin(), bytes.end()});
    peer.jobs.push_back({envelope.kind, std::move(wire), 0});
    return true;
  }
  void Receive(Peer& peer, ae::DataBuffer const& data) {
    AetherLinkEnvelope envelope;
    if (!DeserializeAetherLinkEnvelope(data, envelope, options.max_message_bytes)) {
      ++statistics.malformed_messages; ++peer.statistics.malformed_messages; return;
    }
    switch (envelope.kind) {
      case AetherLinkKind::Probe:
        envelope.kind = AetherLinkKind::Reply;
        (void)Queue(peer, envelope);  // At most one queued echo per peer.
        return;
      case AetherLinkKind::Reply:
        if (peer.presence.ObserveReply(envelope, now_us)) {
          ++statistics.replies_received; ++peer.statistics.replies_received;
        } else { ++statistics.stale_replies; ++peer.statistics.stale_replies; }
        return;
      case AetherLinkKind::Data:
        ++statistics.data_received;
        ++peer.statistics.data_received;
        if (receiver) { receiver(UidText(peer.uid), envelope.payload); }
        return;
    }
  }
  void Pump(Peer& peer) {
    if (peer.terminal) {
      auto const status = *peer.terminal;
      peer.write_sub.Reset(); peer.terminal.reset(); peer.writing = false;
      if (status != ae::WriteAction::Status::kSuccess) {
        ++statistics.write_failures; ++peer.statistics.write_failures; peer.reset_needed = true;
      } else if (!peer.jobs.empty()) {
        peer.last_write_progress_us = now_us;
        auto& job = peer.jobs.front();
        job.offset = peer.written_end;
        if (job.offset == job.wire.size()) {
          if (job.kind == AetherLinkKind::Data) {
            ++statistics.data_completed; ++peer.statistics.data_completed;
          }
          peer.jobs.pop_front();
        }
      }
    }
    auto const expired = peer.presence.Availability(now_us) == LinkAvailability::Offline;
    auto const stalled = now_us >= peer.last_write_progress_us &&
                         now_us - peer.last_write_progress_us >= options.presence_timeout_us;
    if ((peer.reset_needed || (expired && stalled)) && now_us >= peer.session_started_us &&
        now_us - peer.session_started_us >= options.recreate_interval_us) {
      // Reset this collector and stream on a new SDK port. The other endpoint
      // independently handles incoming ports/offline recovery. Business data
      // remains in SharedNode's saved pending queue for retry.
      Bind(peer, client->message_stream_manager().CreatePort(peer.uid));
    }
    if (peer.reset_needed || peer.writing || !peer.safe) { return; }
    if (!Has(peer, AetherLinkKind::Probe)) {
      auto probe = peer.presence.NextProbe(now_us);
      if (probe && Queue(peer, *probe)) { ++statistics.probes_sent; ++peer.statistics.probes_sent; }
    }
    auto const info = peer.safe->stream_info();
    if (peer.jobs.empty() || !info.is_writable || info.max_element_size == 0) { return; }
    auto const& job = peer.jobs.front();
    auto const budget = std::min<std::size_t>(info.max_element_size,
                                             AE_SAFE_STREAM_CAPACITY / 2 - 1);
    auto const end = std::min(job.wire.size(), job.offset + budget);
    ae::DataBuffer chunk{job.wire.begin() + static_cast<std::ptrdiff_t>(job.offset),
                         job.wire.begin() + static_cast<std::ptrdiff_t>(end)};
    peer.written_end = end; peer.writing = true;
    auto const generation = peer.generation;
    auto* const raw_peer = &peer;
    auto& action = peer.safe->Write(std::move(chunk));
    if (action.is_finished()) {
      // A terminal observed before subscription has no readable status in this
      // SDK API. Conservatively retry via the durable upper layer.
      peer.terminal = ae::WriteAction::Status::kFail;
      return;
    }
    peer.write_sub = action.status_event().Subscribe(
        [this, raw_peer, generation](ae::WriteAction::Status status) {
          if (!stopped && raw_peer->generation == generation) { raw_peer->terminal = status; }
        });
  }

  ae::AeContext context;
  ae::Client::ptr client;
  AetherLinkOptions options;
  std::string endpoint;
  IByteLink::Receiver receiver;
  std::map<std::string, std::unique_ptr<Peer>> peers;
  ae::Subscription port_sub;
  AetherLinkStatistics statistics;
  std::uint64_t now_us{0};
  bool serviced{false};
  bool stopped{false};
};

AetherByteLink::AetherByteLink(ae::AeContext const& context, ae::Client::ptr client,
                              AetherLinkOptions options)
    : impl_{std::make_unique<Impl>(context, std::move(client), options)} {}
AetherByteLink::~AetherByteLink() = default;
std::string const& AetherByteLink::EndpointId() const { return impl_->endpoint; }
bool AetherByteLink::WatchPeer(std::string const& endpoint) { return impl_->Watch(endpoint) != nullptr; }
void AetherByteLink::Service(std::uint64_t now_us) {
  assert(!impl_->serviced || now_us >= impl_->now_us);
  impl_->now_us = now_us;
  for (auto const& [key, peer] : impl_->peers) {
    if (!peer->clock_started) {
      peer->presence = AetherPeerPresence{now_us, impl_->options.probe_interval_us,
                                         impl_->options.presence_timeout_us,
                                         peer->sequence_seed};
      peer->clock_started = true; peer->session_started_us = now_us;
    }
    impl_->Pump(*peer);
  }
  impl_->serviced = true;
}
void AetherByteLink::SetReceiver(Receiver receiver) { impl_->receiver = std::move(receiver); }
bool AetherByteLink::Send(std::string const& destination,
                          std::vector<std::uint8_t> const& bytes) {
  if (bytes.empty() || bytes.size() > impl_->options.max_message_bytes) { return false; }
  auto* peer = impl_->Watch(destination);
  if (peer == nullptr || peer->reset_needed ||
      peer->presence.Availability(impl_->now_us) == LinkAvailability::Offline ||
      !peer->safe->stream_info().is_writable || impl_->Has(*peer, AetherLinkKind::Data)) {
    return false;
  }
  AetherLinkEnvelope envelope;
  envelope.payload = bytes;
  if (!impl_->Queue(*peer, envelope)) { return false; }
  ++impl_->statistics.data_accepted;
  ++peer->statistics.data_accepted;
  return true;
}
LinkAvailability AetherByteLink::Availability(std::string const& destination) const {
  ae::Uid uid;
  if (!ParseUid(destination, uid)) { return LinkAvailability::Offline; }
  auto const found = impl_->peers.find(UidText(uid));
  return found == impl_->peers.end() ? LinkAvailability::Unknown
      : found->second->presence.Availability(impl_->now_us);
}
AetherLinkStatistics const& AetherByteLink::stats() const { return impl_->statistics; }
AetherLinkStatistics AetherByteLink::PeerStats(std::string const& destination) const {
  ae::Uid uid;
  if (!ParseUid(destination, uid)) { return {}; }
  auto const found = impl_->peers.find(UidText(uid));
  return found == impl_->peers.end() ? AetherLinkStatistics{} : found->second->statistics;
}

}  // namespace apptraverse
