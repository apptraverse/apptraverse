#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "apptraverse/byte_transport.h"
#include "apptraverse/memory_transport.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

std::string const kA = "endpoint-a";
std::string const kB = "endpoint-b";

struct Note {
  std::string endpoint;
  EndpointAvailability availability{EndpointAvailability::Unknown};
  int calls{0};
};

void OnAvailability(void* ctx, std::string const& endpoint,
                    EndpointAvailability availability) {
  auto* note = static_cast<Note*>(ctx);
  ++note->calls;
  note->endpoint = endpoint;
  note->availability = availability;
}

void TestTransportAvailability() {
  MemoryNetwork network;
  CHECK(network.Availability(kA, kB) == EndpointAvailability::Unknown);
  CHECK(network.Availability(kB, kA) == EndpointAvailability::Unknown);
  CHECK(network.PendingCount(kA, kB) == 0);

  MemoryTransport transport_a(network, kA);
  MemoryTransport transport_b(network, kB);
  Note note_a;
  Note note_b;
  transport_a.BindAvailability(&note_a, &OnAvailability);
  transport_b.BindAvailability(&note_b, &OnAvailability);

  CHECK(transport_a.Availability(kB) == EndpointAvailability::Unknown);
  CHECK(transport_b.Availability(kA) == EndpointAvailability::Unknown);

  network.SetAvailability(kA, kB, EndpointAvailability::Unknown);
  CHECK(note_a.calls == 0);

  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(note_a.calls == 1);
  CHECK(note_a.endpoint == kB);
  CHECK(note_a.availability == EndpointAvailability::Online);
  CHECK(note_b.calls == 0);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Online);
  CHECK(transport_b.Availability(kA) == EndpointAvailability::Unknown);
  CHECK(network.PendingCount(kA, kB) == 0);

  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(note_a.calls == 1);

  network.SetAvailability(kB, kA, EndpointAvailability::Offline);
  CHECK(note_b.calls == 1);
  CHECK(note_b.endpoint == kA);
  CHECK(note_b.availability == EndpointAvailability::Offline);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Online);
  CHECK(network.IsConnected(kA, kB));
  CHECK(network.IsConnected(kB, kA));

  network.Disconnect(kA, kB);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Online);
  CHECK(!network.IsConnected(kA, kB));
  transport_a.Send(kB, std::vector<std::uint8_t>{1, 2, 3});
  CHECK(network.PendingCount(kA, kB) == 0);

  network.Reconnect(kA, kB);
  network.SetAvailability(kA, kB, EndpointAvailability::Offline);
  CHECK(note_a.calls == 2);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Offline);

  transport_a.ClearAvailability();
  network.SetAvailability(kA, kB, EndpointAvailability::Online);
  CHECK(note_a.calls == 2);
  CHECK(transport_a.Availability(kB) == EndpointAvailability::Online);

  MemoryNetwork fresh;
  CHECK(fresh.Availability(kA, kB) == EndpointAvailability::Unknown);
  MemoryTransport restarted(fresh, kA);
  CHECK(restarted.Availability(kB) == EndpointAvailability::Unknown);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestTransportAvailability();
  return 0;
}
