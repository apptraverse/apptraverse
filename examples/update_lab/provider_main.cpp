#include "network.h"
#include "publish.h"
#include <csignal>
#include <iostream>
#include <thread>
using namespace update_lab;
namespace { volatile std::sig_atomic_t stop = 0; void Stop(int) { stop = 1; } }
int main(int argc, char** argv) {
  if (argc >= 2 && std::string{argv[1]} == "publish") return PublishStore(argc, argv);
  fs::path store, profile, endpoint_file;
  bool trace = false;
  for (int n = 1; n + 1 < argc; n += 2) {
    std::string arg = argv[n];
    if (arg == "--store") store = argv[n+1];
    else if (arg == "--profile") profile = argv[n+1];
    else if (arg == "--endpoint-file") endpoint_file = argv[n+1];
    else if (arg == "--trace") trace = std::string{argv[n+1]} == "1";
    else Fail("unknown provider option");
  }
  if (store.empty() || profile.empty()) Fail("provider requires --store and --profile");
  FileLock lock{profile / "provider.lock"};
  if (!lock.acquired()) Fail("provider identity is already in use");
  std::signal(SIGINT, Stop); std::signal(SIGTERM, Stop);
  AetherPeer peer{profile / "identity", "update-lab-component-store"};
  peer.Receive([&](auto const& from, Frame request) {
    auto reply = Serve(store, request);
    std::cout << "COMPONENT " << request.id << " block=" << request.chunk << " bytes=" << reply.payload.size() << std::endl;
    peer.Send(from, std::move(reply));
  });
  bool announced = false;
  auto trace_at = std::chrono::steady_clock::now();
  while (!stop && !peer.failed()) {
    peer.Tick();
    if (trace && std::chrono::steady_clock::now() >= trace_at) {
      std::cout << "TRANSPORT " << peer.Diagnostics() << std::endl;
      trace_at = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    }
    if (!announced && !peer.Endpoint().empty()) {
      auto text = peer.Endpoint();
      std::cout << "PROVIDER " << text << std::endl;
      if (!endpoint_file.empty() && !AtomicWrite(endpoint_file, Bytes{text.begin(), text.end()})) Fail("endpoint file write failed");
      announced = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  return peer.failed() ? 1 : 0;
}
