// SPDX-License-Identifier: Apache-2.0
#include "apptraverse/offline_mailbox_rocksdb.h"

#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
#include <limits>
#include <mutex>

namespace apptraverse::mailbox {
namespace {
std::string U64(std::uint64_t v) {
  std::string s(8, '\0');
  for (int i = 7; i >= 0; --i) {
    s[i] = static_cast<char>(v & 255);
    v >>= 8;
  }
  return s;
}
std::uint64_t Number(std::string_view s) {
  std::uint64_t v = 0;
  for (unsigned char c : s) v = (v << 8) | c;
  return v;
}
std::string Field(std::string const& s) { return s + '\0'; }
std::string Prefix(std::string const& recipient) {
  return "M" + Field(recipient);
}
std::string Identity(Message const& m) {
  return "D" + Field(m.recipient) + Field(m.sender) + Field(m.id);
}
std::string Binary(Bytes const& b) { return {b.begin(), b.end()}; }
Bytes BytesOf(std::string_view s) { return {s.begin(), s.end()}; }
}  // namespace
struct RocksDbMessageStore::Impl {
  std::unique_ptr<rocksdb::DB> db;
  std::mutex mutex;
  bool Get(std::string const& key, std::string& value) {
    auto s = db->Get(rocksdb::ReadOptions{}, key, &value);
    if (s.IsNotFound()) {
      value.clear();
      return true;
    }
    return s.ok();
  }
  bool Counter(std::string const& key, std::uint64_t& value) {
    std::string raw;
    if (!Get(key, raw) || (!raw.empty() && raw.size() != 8)) return false;
    value = Number(raw);
    return true;
  }
  bool Write(rocksdb::WriteBatch& batch) {
    rocksdb::WriteOptions options;
    options.sync = true;  // WAL fsync completes BEFORE any success reply.
    return db->Write(options, &batch).ok();
  }
};
RocksDbMessageStore::RocksDbMessageStore() : impl_{std::make_unique<Impl>()} {}
RocksDbMessageStore::~RocksDbMessageStore() = default;
std::unique_ptr<RocksDbMessageStore> RocksDbMessageStore::Open(
    std::filesystem::path const& path, std::string& error) {
  auto store = std::unique_ptr<RocksDbMessageStore>(new RocksDbMessageStore);
  rocksdb::Options options;
  options.create_if_missing = true;
  options.paranoid_checks = true;
  options.compression = rocksdb::kNoCompression;
  options.max_open_files = 64;
  options.write_buffer_size = 8 * 1024 * 1024;
  rocksdb::DB* db = nullptr;
  auto status = rocksdb::DB::Open(options, path.string(), &db);
  if (!status.ok()) {
    error = status.ToString();
    return {};
  }
  store->impl_->db.reset(db);
  return store;
}
bool RocksDbMessageStore::Store(Message const& m, std::uint64_t now,
                                Entry& result) {
  std::lock_guard lock{impl_->mutex};
  if (m.expires_ms <= now || m.expires_ms - now > kMaxTtlMs ||
      m.sender.empty() || m.recipient.empty() || m.id.empty() ||
      m.sender.size() > 128 || m.recipient.size() > 128 || m.id.size() > 128 ||
      m.sender.find('\0') != std::string::npos ||
      m.recipient.find('\0') != std::string::npos ||
      m.id.find('\0') != std::string::npos || m.blob.size() > kMaxBlob)
    return false;
  auto dedup = Identity(m);
  std::string prior;
  if (!impl_->Get(dedup, prior)) return false;
  if (!prior.empty()) {
    if (prior.size() != 16) return false;
    result = {Number(std::string_view{prior}.substr(0, 8)), m};
    result.message.expires_ms = Number(std::string_view{prior}.substr(8));
    return true;  // First STORE wins, including its original deadline.
  }
  std::uint64_t high;
  auto high_key = "H" + Field(m.recipient);
  if (!impl_->Counter(high_key, high) ||
      high == std::numeric_limits<std::uint64_t>::max())
    return false;
  auto sequence = ++high;
  auto message_key = Prefix(m.recipient) + U64(sequence);
  Command record;
  record.op = Operation::Store;
  record.message = m;
  rocksdb::WriteBatch batch;
  batch.Put(high_key, U64(sequence));
  batch.Put(message_key, Binary(Encode(record)));
  batch.Put(dedup, U64(sequence) + U64(m.expires_ms));
  // Keep dedup tombstones after AckThrough, until the original TTL expires.
  batch.Put("E" + U64(m.expires_ms) + dedup, message_key);
  if (!impl_->Write(batch)) return false;
  result = {sequence, m};
  return true;
}
bool RocksDbMessageStore::Read(std::string const& recipient, std::uint64_t now,
                               std::vector<Entry>& result) {
  std::lock_guard lock{impl_->mutex};
  result.clear();
  auto prefix = Prefix(recipient);
  std::unique_ptr<rocksdb::Iterator> it{
      impl_->db->NewIterator(rocksdb::ReadOptions{})};
  std::size_t bytes = 7;
  for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix);
       it->Next()) {
    if (it->key().size() != prefix.size() + 8) return false;
    auto record = Decode(BytesOf(it->value().ToStringView()));
    if (!record || record->op != Operation::Store) return false;
    if (record->message.expires_ms <= now) continue;
    auto size = it->value().size() + 8;
    if (result.size() == 32 || bytes + size > kMaxFrame - 1024) break;
    bytes += size;
    result.push_back({Number(it->key().ToStringView().substr(prefix.size())),
                      std::move(record->message)});
  }
  if (!it->status().ok()) return false;
  if (!result.empty()) {
    std::uint64_t offered;
    auto key = "O" + Field(recipient);
    if (!impl_->Counter(key, offered)) return false;
    if (result.back().sequence > offered) {
      rocksdb::WriteBatch batch;
      batch.Put(key, U64(result.back().sequence));
      if (!impl_->Write(batch)) return false;
    }
  }
  return true;
}
bool RocksDbMessageStore::AckThrough(std::string const& recipient,
                                     std::uint64_t through,
                                     std::uint64_t& ack) {
  std::lock_guard lock{impl_->mutex};
  std::uint64_t offered;
  auto ack_key = "A" + Field(recipient);
  if (!impl_->Counter(ack_key, ack) ||
      !impl_->Counter("O" + Field(recipient), offered) || through > offered)
    return false;
  if (through <= ack) return true;
  rocksdb::WriteBatch batch;
  auto prefix = Prefix(recipient);
  // DeleteRange plus the cursor commit atomically; inclusive even at
  // UINT64_MAX.
  batch.DeleteRange(prefix, prefix + U64(through));
  batch.Delete(prefix + U64(through));
  batch.Put(ack_key, U64(through));
  if (!impl_->Write(batch)) return false;
  ack = through;
  return true;
}
bool RocksDbMessageStore::Expire(std::uint64_t now) {
  std::lock_guard lock{impl_->mutex};
  std::unique_ptr<rocksdb::Iterator> it{
      impl_->db->NewIterator(rocksdb::ReadOptions{})};
  rocksdb::WriteBatch batch;
  std::size_t count = 0;
  for (it->Seek("E"); it->Valid() && it->key().starts_with("E"); it->Next()) {
    auto key = it->key().ToString();
    if (key.size() < 10) return false;
    if (Number(std::string_view{key}.substr(1, 8)) > now) break;
    batch.Delete(it->value());    // body, if not already acknowledged
    batch.Delete(key.substr(9));  // dedup receipt
    batch.Delete(key);
    if (++count == 256) break;  // bound work on the worker's service turn
  }
  return it->status().ok() && (count == 0 || impl_->Write(batch));
}
}  // namespace apptraverse::mailbox
