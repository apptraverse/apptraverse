// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "apptraverse/byte_link.h"

namespace apptraverse::mailbox {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::size_t kMaxBlob = 900 * 1024;
inline constexpr std::size_t kMaxFrame = 1024 * 1024;
inline constexpr std::uint64_t kMaxTtlMs = 7ULL * 24 * 60 * 60 * 1000;

// Sender ID survives retries. Sequence is a separate, service-assigned,
// monotonically increasing counter in ONE recipient's mailbox.
struct Message {
  std::string sender, recipient, id;
  std::uint64_t expires_ms{};
  Bytes blob;
};
struct Entry {
  std::uint64_t sequence{};
  Message message;
};
enum class Operation : std::uint8_t {
  Store = 1,
  Stored,
  Fetch,
  Batch,
  AckThrough,
  Acked,
  Direct,
  Rejected
};
struct Command {
  Operation op{Operation::Fetch};
  Message message;
  std::string id;
  std::uint64_t through{}, expires_ms{};
  std::vector<Entry> entries;
};
Bytes Encode(Command const&);
std::optional<Command> Decode(Bytes const&);

// Successful Store means stable storage, not just acceptance into RAM.
// Read does not delete. AckThrough is inclusive, recipient-scoped and durable.
// Methods return false on storage errors; the service must NOT ACK failures.
class IMessageStore {
 public:
  virtual ~IMessageStore() = default;
  virtual bool Store(Message const&, std::uint64_t now_ms, Entry&) = 0;
  virtual bool Read(std::string const& recipient, std::uint64_t now_ms,
                    std::vector<Entry>&) = 0;
  virtual bool AckThrough(std::string const& recipient, std::uint64_t through,
                          std::uint64_t& acknowledged) = 0;
  virtual bool Expire(std::uint64_t now_ms) = 0;
};

// The authenticated source comes from Aether, never from application payload.
class ICommandHandler {
 public:
  virtual ~ICommandHandler() = default;
  virtual std::optional<Command> Handle(std::string const& authenticated_source,
                                        Command const&,
                                        std::uint64_t now_ms) = 0;
};
class MailboxService final : public ICommandHandler {
 public:
  explicit MailboxService(IMessageStore& store) : store_{store} {}
  std::optional<Command> Handle(std::string const&, Command const&,
                                std::uint64_t) override;

 private:
  IMessageStore& store_;
};

// Worker-affine transparent decorator. Application still sees ordinary bytes
// and original peer UIDs. The SharedSync protocol owns application-level ACKs.
// Pending mailbox writes and repeat tracking are bounded runtime state;
// application durability across receiver process crashes remains upstream's
// job.
class OfflineMailboxLink final : public IByteLink {
 public:
  struct Statistics {
    std::uint64_t stored{}, rejected{}, expired{}, duplicates{}, malformed{};
    std::size_t pending{};
  };
  OfflineMailboxLink(IByteLink& direct, std::string local_uid,
                     std::string service_uid, std::uint64_t ttl_ms = 86400000);
  ~OfflineMailboxLink() override;
  void Service(std::uint64_t monotonic_us, std::int64_t utc_us);
  void SetRawServiceEndpoint(std::string id);
  void SetReceiver(Receiver) override;
  bool Send(std::string const&, Bytes const&) override;
  bool SendTransient(std::string const&, Bytes const&) override;
  bool SendIdentified(std::string const&, std::string const&,
                      Bytes const&) override;
  void ForgetIdentified(std::string const&, std::string const&) override;
  LinkAvailability Availability(std::string const&) const override;
  Statistics statistics() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace apptraverse::mailbox
