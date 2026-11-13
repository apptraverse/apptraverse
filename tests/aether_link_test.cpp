#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/client_messages/p2p_stream_write_budget.h"
#include "aether/types/packed_size.h"
#include "apptraverse/aether_link.h"
#include "apptraverse/p2p_message_fragments.h"

namespace {

#define CHECK(condition)                                                      \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
      std::exit(1);                                                           \
    }                                                                         \
  } while (false)

using namespace apptraverse;

// Independent server schema: ClientServerApi.adsl.yaml declares
// lastActivityDeltaMs followed by nextPingDeltaMs (both signed long).
struct ServerTiming {
  std::int64_t last_activity;
  std::int64_t next_ping;
  AE_REFLECT_MEMBERS(last_activity, next_ping)
};

void NativeTimingMatchesServerFieldOrder() {
  using namespace std::chrono_literals;
  for (auto server : {ServerTiming{12345, 500}, ServerTiming{20000, -4000}}) {
    std::vector<std::uint8_t> wire;
    ae::seri::BinaryVectorBuffer buffer{wire};
    ae::seri::BinaryArchive archive{buffer};
    CHECK(archive.Save(server).IsOk());
    ae::ClientTiming timing;
    CHECK(archive.Load(timing).IsOk());
    CHECK(timing.next_ping_delta_ms == server.next_ping);
    CHECK(timing.last_connect_delta_ms == server.last_activity);
    auto const now = ae::TimePoint{} + 10s;
    auto const result =
        ae::ClassifyRemoteServerPresence(now, now, now, timing, 1s);
    CHECK(result == (server.next_ping > 0
                         ? ae::RemoteServerPresence::kOnline
                         : ae::RemoteServerPresence::kOffline));
  }
}

ae::Result<ae::PeerPresence, int> NativeState(ae::PeerPresenceState state) {
  return ae::Ok{ae::PeerPresence{state}};
}

void NativePresenceStatesAndLocalSendGate() {
  AetherPresenceSnapshot snapshot;
  CHECK(snapshot.Availability(true, 0, 5000) == LinkAvailability::Unknown);
  snapshot.Observe(NativeState(ae::PeerPresenceState::kOnline), 100);
  CHECK(snapshot.Availability(true, 100, 5000) == LinkAvailability::Online);
  // The same policy gates Send and every new SDK chunk write. Local SDK
  // Offline immediately blocks even a fresh native remote Online result.
  CHECK(snapshot.Availability(false, 100, 5000) == LinkAvailability::Offline);
  CHECK(snapshot.Availability(true, 101, 5000) == LinkAvailability::Online);
  snapshot.Observe(NativeState(ae::PeerPresenceState::kOffline), 200);
  CHECK(snapshot.Availability(true, 200, 5000) == LinkAvailability::Offline);
  snapshot.Observe(NativeState(ae::PeerPresenceState::kUnknown), 300);
  CHECK(snapshot.Availability(true, 300, 5000) == LinkAvailability::Unknown);
}

void FailedAndExpiredNativeQueriesStayUnknown() {
  AetherPresenceSnapshot snapshot;
  snapshot.Observe(NativeState(ae::PeerPresenceState::kOnline), 100);
  snapshot.Observe(ae::Error{3}, 200);
  CHECK(snapshot.Availability(true, 200, 5000) == LinkAvailability::Unknown);
  snapshot.Observe(NativeState(ae::PeerPresenceState::kOnline), 1000);
  CHECK(snapshot.Availability(true, 6000, 5000) == LinkAvailability::Online);
  CHECK(snapshot.Availability(true, 6001, 5000) == LinkAvailability::Unknown);
  // Freshness is measured from the native query start, not delayed receipt.
  snapshot.Observe(NativeState(ae::PeerPresenceState::kOnline), 1000);
  CHECK(snapshot.Availability(true, 7000, 5000) == LinkAvailability::Unknown);
  CHECK(snapshot.Availability(true, 999, 5000) == LinkAvailability::Unknown);
  snapshot.Observe(NativeState(ae::PeerPresenceState::kOffline), 8000);
  CHECK(snapshot.Availability(true, 13001, 5000) == LinkAvailability::Unknown);
  snapshot.Forget();
  CHECK(snapshot.Availability(true, 8000, 5000) == LinkAvailability::Unknown);
}

// Preserve the working streaming fix when adding native presence: TCP's
// unlimited advertised budget must never allocate/encrypt a trial payload.
void SdkUnlimitedTcpBudgetIsBounded() {
  auto const tcp = std::numeric_limits<std::uint32_t>::max();
  auto const raw = ae::MaxP2pStreamWritePayloadBytesForDatagramBudget(tcp);
  CHECK(raw != 0 && raw <= ae::PackedSize::kUpper);
  CHECK(ae::EncodedP2pSendMessageDatagramBytesForInnerPayload(raw) <= tcp);
}

// The adapter's independent fragments preserve opaque native SharedSync bytes
// within the real SDK wire budget, including messages larger than one write.
void SdkFramingPreservesBusinessBytesAndBoundaries() {
  std::vector<std::uint8_t> large(85000);
  for (std::size_t i = 0; i < large.size(); ++i) {
    large[i] = static_cast<std::uint8_t>(i % 251);
  }
  for (auto channel_budget : std::vector<std::size_t>{
           1500, std::numeric_limits<std::uint32_t>::max()}) {
    P2pMessageWriter sender{large};
    P2pMessageReader receiver;
    std::vector<std::vector<std::uint8_t>> packets;
    auto const budget =
        ae::P2pStreamWritePayloadLimitFromChannelMax(channel_budget);
    while (!sender.done()) {
      auto packet = sender.Next(budget);
      CHECK(!packet.empty() &&
            ae::EncodedP2pSendMessageDatagramBytesForInnerPayload(
                packet.size()) <= channel_budget);
      packets.push_back(std::move(packet));
      sender.Advance();
    }
    CHECK(packets.size() > 1);
    for (std::size_t i = packets.size(); --i > 0;)
      CHECK(!receiver.Receive(packets[i], 0));
    auto result = receiver.Receive(packets.front(), 1);
    CHECK(result && *result == large);
  }
}

}  // namespace

int main() {
  NativeTimingMatchesServerFieldOrder();
  NativePresenceStatesAndLocalSendGate();
  FailedAndExpiredNativeQueriesStayUnknown();
  SdkFramingPreservesBusinessBytesAndBoundaries();
  SdkUnlimitedTcpBudgetIsBounded();
  std::cout << "aether_link_test OK\n";
}
