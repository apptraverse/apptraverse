#include <cstdlib>
#include <iostream>
#include <cstdint>

#include "aether_p2p_write_gate.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

using apptraverse::examples::SyncWriteGate;

void TestFirstTryBeginSucceedsAttemptOne() {
  SyncWriteGate<int, int> gate;
  std::uint32_t attempt = 0;
  CHECK(gate.TryBegin(1, 10, attempt));
  CHECK(attempt == 1);
  CHECK(gate.IsInFlight(1, 10));
}

void TestSecondTryBeginSameKeyFails() {
  SyncWriteGate<int, int> gate;
  std::uint32_t attempt = 0;
  CHECK(gate.TryBegin(1, 10, attempt));
  CHECK(attempt == 1);

  std::uint32_t again = 0;
  CHECK(!gate.TryBegin(1, 10, again));
  CHECK(again == 1);
  CHECK(gate.IsInFlight(1, 10));
}

void TestCompleteThenTryBeginSucceedsAttemptTwo() {
  SyncWriteGate<int, int> gate;
  std::uint32_t attempt = 0;
  CHECK(gate.TryBegin(1, 10, attempt));
  CHECK(attempt == 1);
  gate.Complete(1, 10, 1);
  CHECK(!gate.IsInFlight(1, 10));

  CHECK(gate.TryBegin(1, 10, attempt));
  CHECK(attempt == 2);
  CHECK(gate.IsInFlight(1, 10));
}

void TestDifferentPacketIdNotBlocked() {
  SyncWriteGate<int, int> gate;
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  CHECK(gate.TryBegin(1, 10, first));
  CHECK(gate.TryBegin(1, 11, second));
  CHECK(first == 1);
  CHECK(second == 1);
  CHECK(gate.IsInFlight(1, 10));
  CHECK(gate.IsInFlight(1, 11));
}

void TestDifferentPeerNotBlocked() {
  SyncWriteGate<int, int> gate;
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  CHECK(gate.TryBegin(1, 10, first));
  CHECK(gate.TryBegin(2, 10, second));
  CHECK(first == 1);
  CHECK(second == 1);
  CHECK(gate.IsInFlight(1, 10));
  CHECK(gate.IsInFlight(2, 10));
}

void TestStaleCompleteDoesNotUnlock() {
  SyncWriteGate<int, int> gate;
  std::uint32_t attempt = 0;
  CHECK(gate.TryBegin(1, 10, attempt));
  CHECK(attempt == 1);

  gate.Complete(1, 10, 99);
  CHECK(gate.IsInFlight(1, 10));

  std::uint32_t again = 0;
  CHECK(!gate.TryBegin(1, 10, again));
  CHECK(again == 1);

  gate.Complete(1, 10, 1);
  CHECK(!gate.IsInFlight(1, 10));
  CHECK(gate.TryBegin(1, 10, again));
  CHECK(again == 2);

  gate.Complete(1, 10, 1);
  CHECK(gate.IsInFlight(1, 10));
  CHECK(!gate.TryBegin(1, 10, attempt));
  CHECK(attempt == 2);
}

void TestClearPeerDoesNotAffectOtherPeer() {
  SyncWriteGate<int, int> gate;
  std::uint32_t a = 0;
  std::uint32_t b = 0;
  CHECK(gate.TryBegin(1, 10, a));
  CHECK(gate.TryBegin(2, 10, b));
  gate.ClearPeer(1);
  CHECK(!gate.IsInFlight(1, 10));
  CHECK(gate.IsInFlight(2, 10));
}

}  // namespace

int main() {
  TestFirstTryBeginSucceedsAttemptOne();
  TestSecondTryBeginSameKeyFails();
  TestCompleteThenTryBeginSucceedsAttemptTwo();
  TestDifferentPacketIdNotBlocked();
  TestDifferentPeerNotBlocked();
  TestStaleCompleteDoesNotUnlock();
  TestClearPeerDoesNotAffectOtherPeer();
  return 0;
}
