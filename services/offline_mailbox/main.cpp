// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>

#include "apptraverse/aether_connection.h"
#include "apptraverse/offline_mailbox_rocksdb.h"

namespace {
volatile std::sig_atomic_t stopped = 0;
void Stop(int) { stopped = 1; }
}  // namespace
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: apptraverse_mailbox_service IDENTITY_PROFILE "
                 "DATABASE_DIRECTORY\n";
    return 2;
  }
  std::string error;
  auto store = apptraverse::mailbox::RocksDbMessageStore::Open(argv[2], error);
  if (!store) {
    std::cerr << error << '\n';
    return 1;
  }
  apptraverse::mailbox::MailboxService service{*store};
  auto connection =
      apptraverse::OpenAetherConnection(argv[1], "offline-mailbox");
  auto transport = apptraverse::MakeDirectAetherTransport(connection);
  bool bound = false;
  std::uint64_t sweep = 0;
  std::signal(SIGINT, Stop);
  std::signal(SIGTERM, Stop);
  while (!stopped) {
    auto time = apptraverse::ReadConnectionTime();
    transport->Service(time);
    if (transport->Phase() == apptraverse::TransportPhase::Failed) return 1;
    if (transport->Ready() && !bound) {
      std::cout << "MAILBOX_UID " << transport->EndpointId() << std::endl;
      transport->SetReceiver([&](auto const& source, auto const& bytes) {
        auto command = apptraverse::mailbox::Decode(bytes);
        if (!command) return;
        auto now = apptraverse::ReadConnectionTime();
        if (now.utc_us <= 0) return;
        auto reply = service.Handle(source, *command, now.utc_us / 1000);
        // If the link is full, clients repeat STORE/FETCH/ACK. No receipt is
        // fabricated when the database has failed to commit.
        if (reply)
          transport->Send(source, apptraverse::mailbox::Encode(*reply));
      });
      bound = true;
    }
    if (time.monotonic_us >= sweep) {
      if (!store->Expire(time.utc_us / 1000)) {
        std::cerr << "Mailbox expiry failed; stopping\n";
        return 1;
      }
      sweep = time.monotonic_us + 1000000;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  if (bound) transport->SetReceiver({});
  return 0;
}
