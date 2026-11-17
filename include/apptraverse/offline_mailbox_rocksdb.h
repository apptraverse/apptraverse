// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include "apptraverse/offline_mailbox.h"

namespace apptraverse::mailbox {
class RocksDbMessageStore final : public IMessageStore {
 public:
  static std::unique_ptr<RocksDbMessageStore> Open(std::filesystem::path const&,
                                                   std::string& error);
  ~RocksDbMessageStore() override;
  bool Store(Message const&, std::uint64_t, Entry&) override;
  bool Read(std::string const&, std::uint64_t, std::vector<Entry>&) override;
  bool AckThrough(std::string const&, std::uint64_t, std::uint64_t&) override;
  bool Expire(std::uint64_t) override;

 private:
  RocksDbMessageStore();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace apptraverse::mailbox
