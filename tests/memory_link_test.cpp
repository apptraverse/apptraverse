#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "apptraverse/memory_link.h"

namespace {

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed: " #condition << " at " << __LINE__ << '\n'; \
      std::exit(1);                                                         \
    }                                                                       \
  } while (0)

using apptraverse::LinkAvailability;
using apptraverse::MemoryLink;
using apptraverse::MemoryLinkNetwork;

struct Received {
  std::string source;
  std::vector<std::uint8_t> bytes;
};

void TestBytesDelayLossDuplicateAndReordering() {
  MemoryLinkNetwork network;
  MemoryLink a{network, "A"};
  MemoryLink b{network, "B"};
  std::vector<Received> received;
  b.SetReceiver([&](auto const& source, auto const& bytes) {
    received.push_back({source, bytes});
  });

  std::vector<std::uint8_t> source{1, 2, 3};
  bool sent = a.Send("B", source);
  CHECK(sent);
  source[0] = 99;
  auto first = network.PendingPackets().front().id;
  bool delayed = network.Delay(first, 100);
  CHECK(delayed);
  bool delivered = network.Deliver(first);
  CHECK(!delivered);

  sent = a.Send("B", {4});
  CHECK(sent);
  auto second = network.PendingPackets().back().id;
  auto duplicate = network.Duplicate(second);
  sent = a.Send("B", {5});
  CHECK(sent);
  auto lost = network.PendingPackets().back().id;
  bool dropped = network.Drop(lost);
  CHECK(dropped);

  delivered = network.Deliver(duplicate);
  CHECK(delivered);
  delivered = network.Deliver(second);
  CHECK(delivered);
  CHECK(received.size() == 2);
  CHECK(received[0].source == "A");
  CHECK(received[0].bytes == std::vector<std::uint8_t>{4});
  CHECK(received[1].bytes == received[0].bytes);

  network.AdvanceTo(99);
  delivered = network.Deliver(first);
  CHECK(!delivered);
  network.AdvanceTo(100);
  CHECK(received.size() == 2);  // Advancing time never dispatches implicitly.
  delivered = network.Deliver(first);
  CHECK(delivered);
  CHECK(received.size() == 3);
  CHECK((received.back().bytes == std::vector<std::uint8_t>{1, 2, 3}));
  CHECK(network.PendingPackets().empty());
  delivered = network.Deliver(lost);
  CHECK(!delivered);
}

void TestAvailabilityAndIndependentDestinations() {
  MemoryLinkNetwork network;
  MemoryLink a{network, "A"};
  MemoryLink b{network, "B"};
  MemoryLink h{network, "H"};
  std::vector<Received> received;
  auto receive = [&](auto const& source, auto const& bytes) {
    received.push_back({source, bytes});
  };
  b.SetReceiver(receive);
  h.SetReceiver(receive);
  CHECK(a.Availability("B") == LinkAvailability::Unknown);

  network.SetAvailability("B", LinkAvailability::Offline);
  bool sent = a.Send("B", {1});
  CHECK(!sent);
  CHECK(network.PendingPackets().empty());
  sent = a.Send("H", {2});
  CHECK(sent);
  auto host_packet = network.PendingPackets().back().id;
  bool delivered = network.Deliver(host_packet);
  CHECK(delivered);

  network.SetAvailability("B", LinkAvailability::Online);
  sent = a.Send("B", {3});
  CHECK(sent);
  auto packet = network.PendingPackets().back().id;
  network.SetAvailability("B", LinkAvailability::Offline);
  delivered = network.Deliver(packet);
  CHECK(!delivered);
  CHECK(received.size() == 1);
  network.SetAvailability("B", LinkAvailability::Online);
  delivered = network.Deliver(packet);
  CHECK(delivered);
  CHECK(received.size() == 2);

  network.SetAvailability("A", LinkAvailability::Offline);
  sent = a.Send("H", {4});
  CHECK(!sent);
  CHECK(network.PendingPackets().empty());
}

void TestUnbindRestartAndReentrantSend() {
  MemoryLinkNetwork network;
  MemoryLink a{network, "A"};
  std::vector<Received> received;
  auto b = std::make_unique<MemoryLink>(network, "B");
  b->SetReceiver([](auto const&, auto const&) {
    CHECK(false);  // This callback must not survive endpoint destruction.
  });
  network.SetAvailability("B", LinkAvailability::Online);
  bool sent = a.Send("B", {7});
  CHECK(sent);
  auto packet = network.PendingPackets().back().id;
  b.reset();
  CHECK(a.Availability("B") == LinkAvailability::Unknown);
  bool delivered = network.Deliver(packet);
  CHECK(!delivered);

  b = std::make_unique<MemoryLink>(network, "B");
  a.SetReceiver([&](auto const& source, auto const& bytes) {
    received.push_back({source, bytes});
  });
  b->SetReceiver([&](auto const& source, auto const& bytes) {
    CHECK(source == "A");
    CHECK(bytes == std::vector<std::uint8_t>{7});
    bool reply_sent = b->Send("A", {8});
    CHECK(reply_sent);
    b.reset();  // Dispatch owns a callback copy and no endpoint pointer.
  });
  delivered = network.Deliver(packet);
  CHECK(delivered);
  CHECK(network.PendingPackets().size() == 1);
  auto reply = network.PendingPackets().front().id;
  delivered = network.Deliver(reply);
  CHECK(delivered);
  CHECK(received.size() == 1);
  CHECK(received[0].source == "B");
  CHECK(received[0].bytes == std::vector<std::uint8_t>{8});
}

}  // namespace

int main() {
  TestBytesDelayLossDuplicateAndReordering();
  TestAvailabilityAndIndependentDestinations();
  TestUnbindRestartAndReentrantSend();
  std::cout << "memory_link_test OK\n";
}
