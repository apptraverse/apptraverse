#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#include "aether/stream_api/sized_packet_gate.h"
#include "aether/client_messages/p2p_stream_write_budget.h"
#include "aether/safe_stream/safe_stream_wire_budget.h"
#include "aether/types/packed_size.h"
#include "apptraverse/aether_link.h"

namespace {

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
  std::exit(1); } } while (false)

using namespace apptraverse;

void NativeEnvelopeShapesAndBounds() {
  AetherLinkEnvelope data;
  data.payload = {0, 1, 255, 0, 17};
  std::vector<std::uint8_t> bytes;
  bool const encoded = SerializeAetherLinkEnvelope(data, bytes);
  CHECK(encoded);
  AetherLinkEnvelope decoded;
  bool const parsed = DeserializeAetherLinkEnvelope(bytes, decoded);
  CHECK(parsed && decoded.version == 1 && decoded.kind == AetherLinkKind::Data);
  CHECK(decoded.payload == data.payload);
  auto truncated = bytes;
  truncated.pop_back();
  bool const short_read = DeserializeAetherLinkEnvelope(truncated, decoded);
  CHECK(!short_read);
  auto trailing = bytes;
  trailing.push_back(0);
  bool const extra = DeserializeAetherLinkEnvelope(trailing, decoded);
  CHECK(!extra);
  bool const oversized = DeserializeAetherLinkEnvelope(bytes, decoded, 4);
  CHECK(!oversized);
  data.sequence = 1;
  auto const saved = bytes;
  bool const malformed_data = SerializeAetherLinkEnvelope(data, bytes);
  CHECK(!malformed_data && bytes == saved);
  data.sequence = 0; data.version = 2;
  bool const unsupported = SerializeAetherLinkEnvelope(data, bytes);
  CHECK(!unsupported);
  data.version = 1; data.kind = static_cast<AetherLinkKind>(99);
  bool const unknown_kind = SerializeAetherLinkEnvelope(data, bytes);
  CHECK(!unknown_kind);
  AetherLinkEnvelope probe;
  probe.kind = AetherLinkKind::Probe;
  probe.sequence = 9;
  probe.request_time_us = 100;
  bool const probe_encoded = SerializeAetherLinkEnvelope(probe, bytes);
  CHECK(probe_encoded);
  bool const probe_parsed = DeserializeAetherLinkEnvelope(bytes, decoded);
  CHECK(probe_parsed && decoded.kind == AetherLinkKind::Probe && decoded.sequence == 9);
  probe.payload = {1};
  bool const control_payload = SerializeAetherLinkEnvelope(probe, bytes);
  CHECK(!control_payload);
}

void PresenceCorrelatesFreshOriginalRequests() {
  AetherPeerPresence presence;
  CHECK(presence.Availability(0) == LinkAvailability::Unknown);
  auto first = presence.NextProbe(0);
  CHECK(first && first->sequence == 1);
  auto early = presence.NextProbe(999999);
  CHECK(!early);
  auto second = presence.NextProbe(1000000);
  CHECK(second && second->sequence == 2);
  auto reply = *first;
  reply.kind = AetherLinkKind::Reply;
  ++reply.sequence;
  bool const wrong_sequence = presence.ObserveReply(reply, 1500000);
  CHECK(!wrong_sequence);
  reply = *first; reply.kind = AetherLinkKind::Reply; ++reply.request_time_us;
  bool const wrong_time = presence.ObserveReply(reply, 1500000);
  CHECK(!wrong_time);
  reply = *first; reply.kind = AetherLinkKind::Reply;
  bool const fresh = presence.ObserveReply(reply, 1500000);
  CHECK(fresh && presence.Availability(1500000) == LinkAvailability::Online);
  bool const duplicate = presence.ObserveReply(reply, 1500001);
  CHECK(!duplicate);
  CHECK(presence.Availability(5000001) == LinkAvailability::Offline);
  reply = *second; reply.kind = AetherLinkKind::Reply;
  bool const buffered = presence.ObserveReply(reply, 6000001);
  CHECK(!buffered && presence.Availability(6000001) == LinkAvailability::Offline);
  auto recent = presence.NextProbe(7000000);
  CHECK(recent && recent->sequence == 3);
  auto burst = presence.NextProbe(7000000);
  CHECK(!burst);
  reply = *recent; reply.kind = AetherLinkKind::Reply;
  bool const recovered = presence.ObserveReply(reply, 7000100);
  CHECK(recovered && presence.Availability(7000100) == LinkAvailability::Online);
  presence.ResetOutstanding(8000000);
  auto after_reset = presence.NextProbe(8000000);
  CHECK(after_reset && after_reset->sequence == 4);
  bool const old_generation = presence.ObserveReply(reply, 8000100);
  CHECK(!old_generation);
  CHECK(presence.Availability(12000001) == LinkAvailability::Offline);
  // A transport lifetime has its own sequence seed even if its caller starts
  // the relative monotonic clock at zero again after process restart.
  AetherPeerPresence restarted{0, 1000000, 5000000, 100};
  auto new_lifetime = restarted.NextProbe(0);
  CHECK(new_lifetime && new_lifetime->sequence == 101);
  reply = *first; reply.kind = AetherLinkKind::Reply;
  bool const previous_lifetime = restarted.ObserveReply(reply, 100);
  CHECK(!previous_lifetime && restarted.Availability(100) == LinkAvailability::Unknown);
}

void PresenceBookkeepingAndNoCatchupBurst() {
  AetherPeerPresence presence{100, 1, 1000000};
  for (std::uint64_t now = 100; now < 1000; ++now) {
    auto probe = presence.NextProbe(now);
    CHECK(probe);
  }
  CHECK(presence.outstanding_count() == 64);
  AetherPeerPresence delayed{0, 100, 500};
  auto first = delayed.NextProbe(0);
  auto after_pause = delayed.NextProbe(10000);
  CHECK(first && after_pause && after_pause->sequence == 2);
  auto no_burst = delayed.NextProbe(10000);
  CHECK(!no_burst && delayed.outstanding_count() == 1);
  CHECK(delayed.Availability(10000) == LinkAvailability::Offline);
}

// The very same SDK gate used outside P2pSafeStream in AetherByteLink can
// reconstruct messages bigger than the SDK SafeStream circular buffer.
void SdkFramingReassemblesLargeNativeMessages() {
  auto event_system = std::make_unique<ae::EventSystem>();
  struct Context {
    ae::EventSystem* events;
    ae::EventSystem& event_system() const { return *events; }
  } context{event_system.get()};
  ae::SizedPacketGate sender{context};
  ae::SizedPacketGate receiver{context};
  AetherLinkEnvelope large;
  large.payload.resize(85000);
  for (std::size_t i = 0; i < large.payload.size(); ++i) {
    large.payload[i] = static_cast<std::uint8_t>(i % 251);
  }
  std::vector<std::uint8_t> encoded;
  bool const saved = SerializeAetherLinkEnvelope(large, encoded);
  CHECK(saved);
  auto wire = sender.WriteIn(ae::DataBuffer{encoded.begin(), encoded.end()});
  std::vector<std::vector<std::uint8_t>> received;
  ae::Subscription sub = receiver.out_data_event().Subscribe(
      [&](ae::DataBuffer const& message) { received.push_back(message); });
  for (std::size_t offset = 0; offset < wire.size();) {
    auto const end = std::min(wire.size(), offset + 997);
    receiver.WriteOut(ae::DataBuffer{wire.begin() + static_cast<std::ptrdiff_t>(offset),
                                    wire.begin() + static_cast<std::ptrdiff_t>(end)});
    offset = end;
  }
  CHECK(received.size() == 1 && received.front() == encoded);
  AetherLinkEnvelope parsed;
  bool const loaded = DeserializeAetherLinkEnvelope(received.front(), parsed);
  CHECK(loaded && parsed.payload == large.payload);
  sub.Reset();
  receiver.WriteOut(wire);
  CHECK(received.size() == 1);  // Subscription teardown prevents callbacks.
}

// TCP advertises UINT32_MAX. Sizing must remain arithmetic: the earlier
// streaming baseline tried to allocate/encrypt a multi-gigabyte midpoint.
void SdkUnlimitedTcpBudgetIsBounded() {
  auto const tcp = std::numeric_limits<std::uint32_t>::max();
  auto const raw = ae::MaxP2pStreamWritePayloadBytesForDatagramBudget(tcp);
  CHECK(raw != 0 && raw <= ae::PackedSize::kUpper);
  CHECK(ae::EncodedP2pSendMessageDatagramBytesForInnerPayload(raw) <= tcp);
  auto const safe = ae::MaxSafeStreamSendDataBytesForDownstreamMaxElementSize(
      std::numeric_limits<std::size_t>::max());
  CHECK(safe != 0 && safe <= ae::PackedSize::kUpper);
}

}  // namespace

int main() {
  NativeEnvelopeShapesAndBounds();
  PresenceCorrelatesFreshOriginalRequests();
  PresenceBookkeepingAndNoCatchupBurst();
  SdkFramingReassemblesLargeNativeMessages();
  SdkUnlimitedTcpBudgetIsBounded();
  std::cout << "aether_link_test OK\n";
}
