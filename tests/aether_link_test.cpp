#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "aether/stream_api/sized_packet_gate.h"
#include "apptraverse/aether_link.h"

namespace {

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
  std::exit(1); } } while (false)

using namespace apptraverse;

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

// The SDK gate used by AetherByteLink preserves opaque native SharedSync bytes
// through arbitrary chunks, including messages larger than its circular buffer.
void SdkFramingPreservesBusinessBytesAndBoundaries() {
  ae::SizedPacketGate sender;
  ae::SizedPacketGate receiver;
  std::vector<std::uint8_t> large(85000);
  for (std::size_t i = 0; i < large.size(); ++i) {
    large[i] = static_cast<std::uint8_t>(i % 251);
  }
  std::vector<std::uint8_t> const small{0, 1, 255, 0, 17};
  auto wire = sender.WriteIn(ae::DataBuffer{large.begin(), large.end()});
  auto tail = sender.WriteIn(ae::DataBuffer{small.begin(), small.end()});
  wire.insert(wire.end(), tail.begin(), tail.end());
  std::vector<std::vector<std::uint8_t>> received;
  ae::Subscription sub = receiver.out_data_event().Subscribe(
      [&](ae::DataBuffer const& message) { received.push_back(message); });
  for (std::size_t offset = 0; offset < wire.size();) {
    auto const end = std::min(wire.size(), offset + 997);
    receiver.WriteOut(ae::DataBuffer{wire.begin() + static_cast<std::ptrdiff_t>(offset),
                                    wire.begin() + static_cast<std::ptrdiff_t>(end)});
    offset = end;
  }
  CHECK(received.size() == 2 && received[0] == large && received[1] == small);
  sub.Reset();
  receiver.WriteOut(wire);
  CHECK(received.size() == 2);
}

}  // namespace

int main() {
  NativePresenceStatesAndLocalSendGate();
  FailedAndExpiredNativeQueriesStayUnknown();
  SdkFramingPreservesBusinessBytesAndBoundaries();
  std::cout << "aether_link_test OK\n";
}
