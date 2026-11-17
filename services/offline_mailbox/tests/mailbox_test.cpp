// SPDX-License-Identifier: Apache-2.0
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include "apptraverse/offline_mailbox.h"

#define CHECK(x)                             \
  do {                                       \
    if (!(x)) {                              \
      std::cerr << __LINE__ << ": " #x "\n"; \
      std::exit(1);                          \
    }                                        \
  } while (0)
using namespace apptraverse;
using namespace apptraverse::mailbox;
namespace {
struct Link : IByteLink {
  struct Sent {
    std::string to;
    Bytes bytes;
  };
  Receiver receiver;
  std::deque<Sent> sent;
  bool accept{true};
  int budget{-1};
  void SetReceiver(Receiver r) override { receiver = std::move(r); }
  bool Send(std::string const& to, Bytes const& bytes) override {
    if (!accept || budget == 0) return false;
    if (budget > 0) --budget;
    sent.push_back({to, bytes});
    return true;
  }
  LinkAvailability Availability(std::string const&) const override {
    return LinkAvailability::Offline;
  }
  void Inject(std::string const& source, Command const& c) {
    receiver(source, Encode(c));
  }
  void Clear() { sent.clear(); }
};
struct FailingStore : IMessageStore {
  bool Store(Message const&, std::uint64_t, Entry&) override { return false; }
  bool Read(std::string const&, std::uint64_t, std::vector<Entry>&) override {
    return false;
  }
  bool AckThrough(std::string const&, std::uint64_t, std::uint64_t&) override {
    return false;
  }
  bool Expire(std::uint64_t) override { return false; }
};
Command StoreCommand() {
  Command c;
  c.op = Operation::Store;
  c.message = {"alice", "bob", "id", 20000, {0, 1, 0, 255}};
  return c;
}
void Codec() {
  for (auto op : {Operation::Store, Operation::Direct, Operation::Stored,
                  Operation::Fetch, Operation::Batch, Operation::AckThrough,
                  Operation::Acked, Operation::Rejected}) {
    auto c = StoreCommand();
    c.op = op;
    c.id = "id";
    c.through = 42;
    c.expires_ms = 20000;
    c.entries.push_back({42, c.message});
    auto bytes = Encode(c);
    auto decoded = Decode(bytes);
    CHECK(decoded && decoded->op == op);
    CHECK(Encode(*decoded) == bytes);
    for (std::size_t n = 0; n < bytes.size(); ++n) {
      CHECK(!Decode(Bytes(bytes.begin(), bytes.begin() + n)));
    }
    bytes.push_back(0);
    CHECK(!Decode(bytes));
  }
  auto c = StoreCommand();
  c.message.blob.resize(kMaxBlob + 1);
  CHECK(!Decode(Encode(c)));
  Bytes unknown{'A', 'M', 'Q', 1, 255};
  CHECK(!Decode(unknown));
  FailingStore failing;
  MailboxService service{failing};
  c = StoreCommand();
  CHECK(!service.Handle("alice", c, 1000));
  auto rejected = service.Handle("mallory", c, 1000);
  CHECK(rejected && rejected->op == Operation::Rejected);
  c.message.expires_ms = 1000;
  CHECK(service.Handle("alice", c, 1000)->op == Operation::Rejected);
  c.op = Operation::AckThrough;
  c.through = 42;
  CHECK(!service.Handle("bob", c, 1000));
}
void RetryAndReceipt() {
  Link wire;
  OfflineMailboxLink alice{wire, "alice", "service", 10000};
  alice.Service(1, 1000000);
  wire.Clear();
  CHECK(alice.Send("bob", {1, 2}));
  CHECK(wire.sent.size() == 2);
  CHECK(wire.sent[0].to == "service" && wire.sent[1].to == "bob");
  auto original = Decode(wire.sent[0].bytes);
  CHECK(original);
  CHECK(alice.statistics().pending == 1 && alice.statistics().stored == 0);
  auto id = original->message.id;
  auto expires = original->message.expires_ms;
  wire.Clear();
  alice.Service(1000002, 2000000);
  bool retried = false;
  for (auto const& s : wire.sent)
    if (auto c = Decode(s.bytes); c->op == Operation::Store) {
      CHECK(c->message.id == id && c->message.expires_ms == expires);
      retried = true;
    }
  CHECK(retried);
  Command receipt;
  receipt.op = Operation::Stored;
  receipt.id = id;
  receipt.through = 1;
  receipt.expires_ms = expires;
  wire.Inject("mallory", receipt);
  CHECK(alice.statistics().pending == 1);
  wire.Inject("service", receipt);
  CHECK(alice.statistics().pending == 0);
  CHECK(alice.statistics().stored == 1);
  wire.Clear();
  alice.Service(2000003, 3000000);
  for (auto const& s : wire.sent)
    CHECK(Decode(s.bytes)->op != Operation::Store);
  CHECK(alice.Send("bob", {1, 2}));
  CHECK(Decode(wire.sent[wire.sent.size() - 2].bytes)->message.id != id);
  alice.Service(20000000, 30000000);
  CHECK(alice.statistics().expired == 1);
}
void CumulativeReceive() {
  Link wire;
  OfflineMailboxLink bob{wire, "bob", "service", 10000};
  std::vector<Bytes> delivered;
  bob.SetReceiver([&](auto const& sender, auto const& bytes) {
    CHECK(sender == "alice");
    delivered.push_back(bytes);
  });
  bob.Service(1, 1000000);
  wire.Clear();
  auto direct = StoreCommand();
  direct.op = Operation::Direct;
  wire.Inject("mallory", direct);
  CHECK(delivered.empty());
  wire.Inject("alice", direct);
  CHECK(delivered.size() == 1);
  Command batch;
  batch.op = Operation::Batch;
  batch.entries = {{4, direct.message}};
  auto second = direct.message;
  second.id = "id-2";
  second.blob = {8};
  batch.entries.push_back({7, second});
  wire.Inject("service", batch);
  CHECK(delivered.size() == 3);
  CHECK(Decode(wire.sent.back().bytes)->through == 7);
  wire.Inject("service", batch);
  CHECK(delivered.size() == 5);
  CHECK(bob.statistics().duplicates == 3);
  Command acked;
  acked.op = Operation::Acked;
  acked.through = 7;
  wire.Inject("service", acked);
  wire.Clear();
  bob.Service(1000002, 2000000);
  CHECK(wire.sent.size() == 1 &&
        Decode(wire.sent.front().bytes)->op == Operation::Fetch);
  // A gap in server sequence numbers is legitimate after TTL expiry, but
  // decreasing/out-of-order sequences in a single batch must not be ACKed.
  wire.Clear();
  std::swap(batch.entries[0], batch.entries[1]);
  wire.Inject("service", batch);
  CHECK(wire.sent.empty());
  batch.entries = {{8, second}};
  batch.entries[0].message.recipient = "other";
  wire.Inject("service", batch);
  CHECK(wire.sent.empty());
}
void StableNativePacketsAndBypass() {
  Link wire;
  OfflineMailboxLink alice{wire, "alice", "service", 10000};
  alice.Service(1, 1000000);
  wire.Clear();
  CHECK(alice.SendIdentified("bob", "node:packet", {5}));
  auto store = Decode(wire.sent.front().bytes);
  CHECK(store);
  Command receipt;
  receipt.op = Operation::Stored;
  receipt.id = store->message.id;
  receipt.through = 1;
  receipt.expires_ms = store->message.expires_ms;
  wire.Inject("service", receipt);
  wire.Clear();
  CHECK(alice.SendIdentified("bob", "node:packet", {5}));
  CHECK(wire.sent.size() == 1 && wire.sent.front().to == "bob");
  CHECK(Decode(wire.sent.front().bytes)->message.id == store->message.id);
  CHECK(!alice.SendIdentified("bob", "node:packet", {6}));
  alice.ForgetIdentified("bob", "node:packet");
  wire.Clear();
  CHECK(alice.SendIdentified("bob", "node:packet", {6}));
  CHECK(wire.sent.size() == 2);
  alice.SetRawServiceEndpoint("push");
  wire.Clear();
  CHECK(alice.Send("push", {3, 0, 9}));
  CHECK(wire.sent.size() == 1 && wire.sent.front().bytes == Bytes({3, 0, 9}));
}
void BackpressureAndLostDeleteAck() {
  Link wire;
  OfflineMailboxLink bob{wire, "bob", "service", 10000};
  bob.Service(1, 1000000);
  wire.Clear();
  auto message = StoreCommand().message;
  Command batch;
  batch.op = Operation::Batch;
  batch.entries = {{1, message}};
  wire.Inject("service", batch);
  CHECK(wire.sent.empty());  // no reader, no delete
  bob.SetReceiver([](auto const&, auto const&) {});
  wire.Inject("service", batch);
  CHECK(Decode(wire.sent.back().bytes)->op == Operation::AckThrough);
  wire.Clear();
  bob.Service(1000002, 2000000);
  CHECK(Decode(wire.sent.front().bytes)->op == Operation::AckThrough);
  CHECK(Decode(wire.sent.front().bytes)->through ==
        1);  // lost ACKED is retried
  Link down;
  down.accept = false;
  OfflineMailboxLink alice{down, "alice", "service", 10000};
  alice.Service(1, 1000000);
  for (int i = 0; i < 128; ++i) CHECK(alice.Send("bob", {1}));
  CHECK(!alice.Send("bob", {1}));
  CHECK(alice.statistics().pending == 128);
  down.accept = true;
  CHECK(alice.SendTransient("bob", {9}));
  CHECK(down.sent.size() == 1 && down.sent.front().to == "bob");
  CHECK(Decode(down.sent.front().bytes)->op == Operation::Direct);
  CHECK(alice.statistics().pending == 128);  // ACKs need no service queue slot
}
void SingleSlotFairness() {
  Link wire;
  wire.budget = 1;
  OfflineMailboxLink link{wire, "alice", "service", 10000};
  link.Service(1, 1000000);  // initial FETCH consumes the only transport slot
  CHECK(link.SendIdentified("bob", "packet", {7}));
  wire.Clear();
  wire.budget = 1;
  link.Service(1000002, 2000000);  // periodic FETCH gets a fair slot
  CHECK(Decode(wire.sent.front().bytes)->op == Operation::Fetch);
  wire.Clear();
  wire.budget = 1;
  link.Service(1000003,
               2000001);  // rejected STORE need not wait another second
  CHECK(wire.sent.size() == 1 &&
        Decode(wire.sent.front().bytes)->op == Operation::Store);
  CHECK(link.statistics().pending == 1);
  link.ForgetIdentified("bob",
                        "packet");  // native ACK releases auxiliary retry
  CHECK(link.statistics().pending == 0);
}
}  // namespace
int main() {
  Codec();
  RetryAndReceipt();
  CumulativeReceive();
  StableNativePacketsAndBypass();
  BackpressureAndLostDeleteAck();
  SingleSlotFairness();
  std::cout << "mailbox protocol/client checks passed\n";
}
