// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include "apptraverse/offline_mailbox_rocksdb.h"

#define CHECK(x)                             \
  do {                                       \
    if (!(x)) {                              \
      std::cerr << __LINE__ << ": " #x "\n"; \
      std::exit(1);                          \
    }                                        \
  } while (0)
using namespace apptraverse::mailbox;
namespace {
struct Network {
  struct Packet {
    std::string from, to;
    Bytes bytes;
  };
  std::deque<Packet> packets;
  std::map<std::string, apptraverse::IByteLink::Receiver> readers;
  bool lose_stored{true}, lose_acked{true};
  void Drain(IMessageStore& store, std::uint64_t now) {
    MailboxService service{store};
    int budget = 1000;
    while (!packets.empty()) {
      CHECK(--budget > 0);
      auto p = std::move(packets.front());
      packets.pop_front();
      if (p.to == "service") {
        auto c = Decode(p.bytes);
        CHECK(c);
        if (auto reply = service.Handle(p.from, *c, now)) {
          if (reply->op == Operation::Stored && lose_stored) {
            lose_stored = false;
            continue;
          }
          if (reply->op == Operation::Acked && lose_acked) {
            lose_acked = false;
            continue;
          }
          packets.push_back({"service", p.from, Encode(*reply)});
        }
      } else if (auto it = readers.find(p.to);
                 it != readers.end() && it->second) {
        it->second(p.from, p.bytes);
      }
    }
  }
};
struct Wire : apptraverse::IByteLink {
  Network& network;
  std::string name;
  Wire(Network& n, std::string id) : network{n}, name{std::move(id)} {}
  void SetReceiver(Receiver r) override {
    network.readers[name] = std::move(r);
  }
  bool Send(std::string const& to, Bytes const& bytes) override {
    network.packets.push_back({name, to, bytes});
    return true;
  }
  apptraverse::LinkAvailability Availability(
      std::string const&) const override {
    return apptraverse::LinkAvailability::Unknown;
  }
};
void OfflineRoundTrip(std::unique_ptr<RocksDbMessageStore>& store,
                      std::filesystem::path const& path) {
  Network network;
  Wire sender{network, "erin"};
  {
    OfflineMailboxLink link{sender, "erin", "service", 10000};
    link.Service(1, 21000000);
    CHECK(link.SendIdentified("dave", "native-packet", {0, 8, 0}));
    network.Drain(*store, 21000);
    CHECK(link.statistics().pending == 1);  // STORE reply lost, Dave offline
    link.Service(1000002, 22000000);
    network.Drain(*store, 22000);
    CHECK(link.statistics().pending == 0 && link.statistics().stored == 1);
  }  // sender is now offline
  store.reset();
  std::string error;
  store = RocksDbMessageStore::Open(path, error);
  CHECK(store);
  Wire recipient{network, "dave"};
  OfflineMailboxLink link{recipient, "dave", "service", 10000};
  int delivered = 0;
  link.SetReceiver([&](auto const& from, auto const& blob) {
    CHECK(from == "erin" && blob == Bytes({0, 8, 0}));
    ++delivered;
  });
  link.Service(2000003, 23000000);
  network.Drain(*store, 23000);
  CHECK(delivered == 1);
  // Cumulative deletion committed even though its ACKED response was lost.
  std::vector<Entry> entries;
  CHECK(store->Read("dave", 23000, entries) && entries.empty());
  link.Service(3000004, 24000000);
  network.Drain(*store, 24000);
  CHECK(delivered == 1);
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 3) {
    std::string error;
    auto store = RocksDbMessageStore::Open(argv[2], error);
    CHECK(store);
    Entry e;
    if (std::string{argv[1]} == "--crash-write") {
      Message m{"alice", "bob", "before-crash", 10000, {0, 7, 0}};
      CHECK(store->Store(m, 1000, e));
      std::_Exit(0);  // no DB destructor, no orderly flush/checkpoint
    }
    CHECK(std::string{argv[1]} == "--check-crash");
    std::vector<Entry> entries;
    CHECK(store->Read("bob", 1001, entries) && entries.size() == 1);
    CHECK(entries.front().sequence == 1 &&
          entries.front().message.blob == Bytes({0, 7, 0}));
    Message m{"alice", "bob", "after-crash", 10000, {9}};
    CHECK(store->Store(m, 1001, e) && e.sequence == 2);
    return 0;
  }
  auto path =
      std::filesystem::temp_directory_path() /
      ("apptraverse-mailbox-test-" + std::to_string(std::random_device{}()));
  std::string error;
  auto store = RocksDbMessageStore::Open(path, error);
  CHECK(store);
  Entry result;
  Message m{"alice", "bob", "one", 10000, {0, 9, 0, 255}};
  CHECK(store->Store(m, 1000, result) && result.sequence == 1);
  m.expires_ms = 20000;
  CHECK(store->Store(m, 1000, result) && result.sequence == 1);
  CHECK(result.message.expires_ms == 10000);  // retries never extend retention
  m.id = "two";
  m.expires_ms = 3000;
  CHECK(store->Store(m, 1000, result) && result.sequence == 2);
  m.id = "three";
  m.expires_ms = 20000;
  CHECK(store->Store(m, 1000, result) && result.sequence == 3);
  m.recipient = "carol";
  CHECK(store->Store(m, 1000, result) && result.sequence == 1);
  std::uint64_t acknowledged;
  CHECK(!store->AckThrough("bob", 3, acknowledged));  // never offered
  std::vector<Entry> entries;
  CHECK(store->Read("bob", 4000, entries) && entries.size() == 2);
  CHECK(entries[0].sequence == 1 && entries[1].sequence == 3);
  CHECK(entries[0].message.blob == m.blob);
  CHECK(store->AckThrough("bob", 1, acknowledged) && acknowledged == 1);
  // Restart with no graceful checkpoint: WAL restores counter, data and ACK.
  store.reset();
  store = RocksDbMessageStore::Open(path, error);
  CHECK(store);
  CHECK(store->Read("bob", 4000, entries) && entries.size() == 1);
  CHECK(entries.front().sequence == 3);
  m.recipient = "bob";
  m.id = "one";
  m.expires_ms = 10000;
  CHECK(store->Store(m, 4000, result) && result.sequence == 1);
  CHECK(store->Read("bob", 4000, entries) &&
        entries.size() == 1);  // no resurrection
  CHECK(store->AckThrough("bob", 3, acknowledged) && acknowledged == 3);
  CHECK(store->AckThrough("bob", 1, acknowledged) &&
        acknowledged == 3);  // stale ACK
  CHECK(!store->AckThrough("bob", 4, acknowledged));
  CHECK(store->Read("carol", 4000, entries) && entries.size() == 1);
  CHECK(store->Expire(20000));
  CHECK(store->Read("carol", 20000, entries) && entries.empty());
  m.id = "four";
  m.expires_ms = 40000;
  CHECK(store->Store(m, 20000, result) && result.sequence == 4);
  store.reset();
  store = RocksDbMessageStore::Open(path, error);
  CHECK(store);
  CHECK(store->Read("bob", 20001, entries) && entries.front().sequence == 4);
  // Bounded fetch across more than one batch and an inclusive prefix ACK.
  for (int i = 0; i < 40; ++i) {
    m.id = "batch-" + std::to_string(i);
    CHECK(store->Store(m, 20001, result));
  }
  CHECK(store->Read("bob", 20001, entries) && entries.size() == 32);
  auto through = entries.back().sequence;
  CHECK(store->AckThrough("bob", through, acknowledged));
  CHECK(store->Read("bob", 20001, entries) && entries.size() == 9);
  CHECK(entries.front().sequence == through + 1);
  // Authenticated source owns fetch/ACK scope; an envelope cannot select Bob.
  MailboxService service{*store};
  Command c;
  c.op = Operation::AckThrough;
  c.through = through;
  CHECK(!service.Handle("mallory", c, 20001));
  c.op = Operation::Fetch;
  auto reply = service.Handle("mallory", c, 20001);
  CHECK(reply && reply->entries.empty());
  OfflineRoundTrip(store, path);
  store.reset();
  // Only remove the unique temporary database created by this test.
  std::filesystem::remove_all(path);
  std::cout
      << "RocksDB durability, TTL, deduplication and cursor checks passed\n";
}
