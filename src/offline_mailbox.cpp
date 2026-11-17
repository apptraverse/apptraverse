// SPDX-License-Identifier: Apache-2.0
#include "apptraverse/offline_mailbox.h"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <random>
#include <utility>

namespace apptraverse::mailbox {
namespace {
constexpr std::array<std::uint8_t, 4> kMagic{'A', 'M', 'Q', 1};
bool Name(std::string const& s) {
  return !s.empty() && s.size() <= 128 && s.find('\0') == std::string::npos;
}
struct Writer {
  Bytes bytes{kMagic.begin(), kMagic.end()};
  void Number(std::uint64_t v, int n = 8) {
    for (int i = n - 1; i >= 0; --i) bytes.push_back((v >> (i * 8)) & 255);
  }
  void String(std::string const& s) {
    Number(s.size(), 2);
    bytes.insert(bytes.end(), s.begin(), s.end());
  }
  void MessageValue(Message const& m) {
    String(m.sender);
    String(m.recipient);
    String(m.id);
    Number(m.expires_ms);
    Number(m.blob.size(), 4);
    bytes.insert(bytes.end(), m.blob.begin(), m.blob.end());
  }
};
struct Reader {
  Bytes const& bytes;
  std::size_t pos{4};
  bool ok{true};
  std::uint64_t Number(std::size_t n = 8) {
    if (n > bytes.size() - pos) {
      ok = false;
      return 0;
    }
    std::uint64_t v = 0;
    while (n--) v = (v << 8) | bytes[pos++];
    return v;
  }
  std::string String() {
    auto n = Number(2);
    if (!ok || n > 128 || n > bytes.size() - pos) {
      ok = false;
      return {};
    }
    std::string s(bytes.begin() + pos, bytes.begin() + pos + n);
    pos += n;
    if (!Name(s)) ok = false;
    return s;
  }
  Message MessageValue() {
    Message m{String(), String(), String(), Number(), {}};
    auto n = Number(4);
    if (!ok || n > kMaxBlob || n > bytes.size() - pos) {
      ok = false;
      return m;
    }
    m.blob.assign(bytes.begin() + pos, bytes.begin() + pos + n);
    pos += n;
    return m;
  }
};
std::string NewEpoch() {
  std::random_device random;
  constexpr char hex[] = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < 16; ++i) {
    auto v = random() & 255;
    s.push_back(hex[v >> 4]);
    s.push_back(hex[v & 15]);
  }
  return s;
}
}  // namespace

Bytes Encode(Command const& c) {
  Writer w;
  w.Number(static_cast<std::uint8_t>(c.op), 1);
  switch (c.op) {
    case Operation::Store:
    case Operation::Direct:
      w.MessageValue(c.message);
      break;
    case Operation::Stored:
      w.String(c.id);
      w.Number(c.through);
      w.Number(c.expires_ms);
      break;
    case Operation::Fetch:
      break;
    case Operation::Batch:
      w.Number(c.entries.size(), 2);
      for (auto const& e : c.entries) {
        w.Number(e.sequence);
        w.MessageValue(e.message);
      }
      break;
    case Operation::AckThrough:
    case Operation::Acked:
      w.Number(c.through);
      break;
    case Operation::Rejected:
      w.String(c.id);
      break;
  }
  return w.bytes;
}
std::optional<Command> Decode(Bytes const& bytes) {
  if (bytes.size() < 5 || bytes.size() > kMaxFrame ||
      !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
    return {};
  Reader r{bytes};
  Command c;
  c.op = static_cast<Operation>(r.Number(1));
  switch (c.op) {
    case Operation::Store:
    case Operation::Direct:
      c.message = r.MessageValue();
      break;
    case Operation::Stored:
      c.id = r.String();
      c.through = r.Number();
      c.expires_ms = r.Number();
      break;
    case Operation::Fetch:
      break;
    case Operation::Batch: {
      auto n = r.Number(2);
      if (n > 32) return {};
      while (n-- && r.ok) {
        auto seq = r.Number();
        c.entries.push_back({seq, r.MessageValue()});
      }
      break;
    }
    case Operation::AckThrough:
    case Operation::Acked:
      c.through = r.Number();
      break;
    case Operation::Rejected:
      c.id = r.String();
      break;
    default:
      return {};
  }
  if (!r.ok || r.pos != bytes.size()) return {};
  return c;
}
std::optional<Command> MailboxService::Handle(std::string const& source,
                                              Command const& c,
                                              std::uint64_t now) {
  if (!Name(source)) return {};
  Command reply;
  switch (c.op) {
    case Operation::Store: {
      auto const& m = c.message;
      if (m.sender != source || !Name(m.recipient) || !Name(m.id) ||
          m.blob.size() > kMaxBlob || m.expires_ms <= now ||
          m.expires_ms - now > kMaxTtlMs) {
        if (!Name(m.id)) return {};
        reply.op = Operation::Rejected;
        reply.id = m.id;
        return reply;
      }
      Entry stored;
      if (!store_.Store(m, now, stored)) return {};
      reply.op = Operation::Stored;
      reply.id = m.id;
      reply.through = stored.sequence;
      reply.expires_ms = stored.message.expires_ms;
      return reply;
    }
    case Operation::Fetch:
      reply.op = Operation::Batch;
      if (!store_.Read(source, now, reply.entries)) return {};
      return reply;
    case Operation::AckThrough:
      reply.op = Operation::Acked;
      if (!store_.AckThrough(source, c.through, reply.through)) return {};
      return reply;
    default:
      return {};
  }
}

struct OfflineMailboxLink::Impl {
  struct Pending {
    Message message;
    std::uint64_t next{};
  };
  IByteLink* direct;
  std::string local_uid;
  std::map<std::string, Message> identified;
  std::string service, raw_service, epoch{NewEpoch()};
  std::uint64_t ttl, counter{}, now{}, next_poll{}, ack{};
  std::int64_t utc{};
  bool bound{};
  Receiver receiver;
  std::map<std::string, Pending> pending;
  std::map<std::string, std::uint64_t> seen;
  std::size_t pending_bytes{};
  Statistics stats;

  Impl(IByteLink& d, std::string local, std::string s, std::uint64_t t)
      : direct{&d},
        local_uid{std::move(local)},
        service{std::move(s)},
        ttl{t} {}
  std::uint64_t Wall() const {
    return utc > 0 ? static_cast<std::uint64_t>(utc) / 1000 : 0;
  }
  bool Deliver(Message const& m) {
    if (m.recipient != local_uid || !Name(m.sender) || !Name(m.id))
      return false;
    if (m.expires_ms <= Wall())
      return true;  // expired data may be acknowledged
    auto key = m.sender + '\0' + m.id;
    if (!receiver) return false;
    if (seen.contains(key))
      ++stats.duplicates;
    else if (seen.size() < 65536)
      seen.emplace(std::move(key), m.expires_ms);
    // Delivery is at-least-once. SharedSync must see repeats to regenerate a
    // lost application ACK (and to retry an earlier rejected bootstrap frame).
    // Its native packet/Event IDs prevent applying an Event twice.
    receiver(m.sender, m.blob);
    return true;
  }
  void Receive(std::string const& source, Bytes const& bytes) {
    auto c = Decode(bytes);
    if (!c) {
      // Accept ordinary incoming application bytes, but never leak a malformed
      // mailbox envelope into the application protocol.
      if (source != service && (bytes.size() < 3 || bytes[0] != 'A' ||
                                bytes[1] != 'M' || bytes[2] != 'Q')) {
        if (receiver) receiver(source, bytes);
      } else
        ++stats.malformed;
      return;
    }
    if (source != service) {
      if (c->op == Operation::Direct && c->message.sender == source)
        Deliver(c->message);
      else
        ++stats.malformed;
      return;
    }
    if (c->op == Operation::Stored) {
      auto it = pending.find(c->id);
      if (it != pending.end() &&
          c->expires_ms == it->second.message.expires_ms && c->through) {
        pending_bytes -= it->second.message.blob.size();
        pending.erase(it);
        ++stats.stored;
      }
    } else if (c->op == Operation::Rejected) {
      if (auto it = pending.find(c->id); it != pending.end()) {
        pending_bytes -= it->second.message.blob.size();
        pending.erase(it);
        ++stats.rejected;
      }
    } else if (c->op == Operation::Acked) {
      if (c->through >= ack) ack = 0;
    } else if (c->op == Operation::Batch) {
      std::uint64_t through = 0;
      // Validate the ENTIRE batch before publishing anything or deleting a
      // prefix.
      for (auto const& e : c->entries) {
        if (e.sequence <= through || e.message.recipient != local_uid) {
          ++stats.malformed;
          return;
        }
        through = e.sequence;
      }
      for (auto const& e : c->entries)
        if (!Deliver(e.message)) return;
      ack = std::max(ack, through);
      if (ack) {
        Command a;
        a.op = Operation::AckThrough;
        a.through = ack;
        direct->Send(service, Encode(a));
      }
    }
  }
};
OfflineMailboxLink::OfflineMailboxLink(IByteLink& direct, std::string local,
                                       std::string service, std::uint64_t ttl)
    : impl_{std::make_unique<Impl>(direct, std::move(local), std::move(service),
                                   ttl)} {}
OfflineMailboxLink::~OfflineMailboxLink() {
  if (impl_->bound) impl_->direct->SetReceiver({});
}
void OfflineMailboxLink::Service(std::uint64_t monotonic_us,
                                 std::int64_t utc_us) {
  auto& p = *impl_;
  p.now = monotonic_us;
  p.utc = utc_us;
  if (!p.bound) {
    p.direct->SetReceiver(
        [&p](auto const& s, auto const& b) { p.Receive(s, b); });
    p.bound = true;
  }
  for (auto it = p.seen.begin(); it != p.seen.end();) {
    if (it->second <= p.Wall())
      it = p.seen.erase(it);
    else
      ++it;
  }
  for (auto it = p.pending.begin(); it != p.pending.end();) {
    if (it->second.message.expires_ms <= p.Wall()) {
      p.pending_bytes -= it->second.message.blob.size();
      it = p.pending.erase(it);
      ++p.stats.expired;
    } else
      ++it;
  }
  // Polls get a periodic slot even during a large outgoing backlog.
  if (p.next_poll <= p.now) {
    p.next_poll = p.now + 1000000;
    if (p.ack) {
      Command a;
      a.op = Operation::AckThrough;
      a.through = p.ack;
      p.direct->Send(p.service, Encode(a));
    }
    Command c;
    c.op = Operation::Fetch;
    p.direct->Send(p.service, Encode(c));
  }
  // Snapshot IDs: a test/in-process transport may invoke Stored synchronously.
  std::vector<std::string> due;
  for (auto const& [id, v] : p.pending)
    if (v.next <= p.now) due.push_back(id);
  for (auto const& id : due) {
    auto it = p.pending.find(id);
    if (it == p.pending.end()) continue;
    it->second.next = p.now + 1000000;
    Command c;
    c.op = Operation::Store;
    c.message = it->second.message;
    if (!p.direct->Send(p.service, Encode(c))) {
      if (auto refused = p.pending.find(id); refused != p.pending.end())
        refused->second.next =
            p.now;  // local refusal consumes no remote timeout
    }
  }
}
bool OfflineMailboxLink::Send(std::string const& recipient,
                              Bytes const& bytes) {
  auto& p = *impl_;
  if (!p.raw_service.empty() && recipient == p.raw_service)
    return p.direct->Send(recipient, bytes);
  if (!p.bound || !Name(recipient) || recipient == p.service ||
      bytes.size() > kMaxBlob || p.ttl == 0 || p.ttl > kMaxTtlMs || !p.Wall())
    return false;
  // Every accepted Send is a new message, even for byte-identical blobs.
  // Only retries of the internal STORE operation reuse this ID.
  if (p.pending.size() >= 128 ||
      p.pending_bytes + bytes.size() > 16 * 1024 * 1024 ||
      p.counter == std::numeric_limits<std::uint64_t>::max())
    return false;
  auto id = p.epoch + ":" + std::to_string(++p.counter);
  Message m{p.local_uid, recipient, id, p.Wall() + p.ttl, bytes};
  p.pending.emplace(id, Impl::Pending{m, p.now + 1000000});
  p.pending_bytes += bytes.size();
  Command c;
  c.op = Operation::Store;
  c.message = m;
  p.direct->Send(p.service,
                 Encode(c));  // Never infer storage from Send's bool.
  c.op = Operation::Direct;
  p.direct->Send(recipient, Encode(c));
  return true;
}
void OfflineMailboxLink::SetReceiver(Receiver r) {
  impl_->receiver = std::move(r);
}
bool OfflineMailboxLink::SendTransient(std::string const& recipient,
                                       Bytes const& bytes) {
  auto& p = *impl_;
  if (!p.bound || !Name(recipient) || recipient == p.service ||
      bytes.size() > kMaxBlob || !p.Wall() ||
      p.counter == std::numeric_limits<std::uint64_t>::max())
    return false;
  Command c;
  c.op = Operation::Direct;
  c.message = {p.local_uid, recipient,
               p.epoch + ":" + std::to_string(++p.counter), p.Wall() + p.ttl,
               bytes};
  return p.direct->Send(recipient, Encode(c));
}
void OfflineMailboxLink::SetRawServiceEndpoint(std::string id) {
  impl_->raw_service = std::move(id);
}
// SharedSync supplies its already-existing immutable packet identity, so its
// own retry loop does not enqueue a second mailbox copy or renew the TTL.
bool OfflineMailboxLink::SendIdentified(std::string const& recipient,
                                        std::string const& identity,
                                        Bytes const& bytes) {
  auto& p = *impl_;
  auto key = recipient + '\0' + identity;
  auto it = p.identified.find(key);
  if (it != p.identified.end()) {
    if (it->second.blob != bytes) return false;
    Command c;
    c.op = Operation::Direct;
    c.message = it->second;
    // TTL limits the service's retention, not the upstream retry obligation.
    c.message.expires_ms = p.Wall() + p.ttl;
    p.direct->Send(recipient, Encode(c));
    return true;
  }
  std::size_t retained_bytes = bytes.size();
  for (auto const& [saved_key, saved] : p.identified)
    retained_bytes += saved.blob.size();
  if (p.identified.size() >= 4096 || retained_bytes > 16 * 1024 * 1024)
    return false;
  // Install BEFORE Send: a synchronous receiver can complete the obligation.
  // Send uses the same next counter for the actual wire ID.
  auto id = p.epoch + ":" + std::to_string(p.counter + 1);
  Message m{p.local_uid, recipient, id, p.Wall() + p.ttl, bytes};
  p.identified.emplace(key, std::move(m));
  if (!Send(recipient, bytes)) {
    p.identified.erase(key);
    return false;
  }
  return true;
}
void OfflineMailboxLink::ForgetIdentified(std::string const& recipient,
                                          std::string const& identity) {
  auto& p = *impl_;
  auto it = p.identified.find(recipient + '\0' + identity);
  if (it == p.identified.end()) return;
  // The native receiver ACK has completed this obligation, so an unconfirmed
  // auxiliary STORE no longer needs to fill the sender's retry queue.
  if (auto pending = p.pending.find(it->second.id);
      pending != p.pending.end()) {
    p.pending_bytes -= pending->second.message.blob.size();
    p.pending.erase(pending);
  }
  p.identified.erase(it);
}
LinkAvailability OfflineMailboxLink::Availability(std::string const& id) const {
  return Name(id) ? LinkAvailability::Unknown : LinkAvailability::Offline;
}
OfflineMailboxLink::Statistics OfflineMailboxLink::statistics() const {
  auto s = impl_->stats;
  s.pending = impl_->pending.size();
  return s;
}
}  // namespace apptraverse::mailbox
