#pragma once
#include "apptraverse/connection_runtime.h"
namespace apptraverse {
// One SDK connection/worker per process (or explicitly selected identity).
// Each graph leases a channel with an independently consumed receiver queue.
// Channel IDs are application composition, e.g. a stable document/pair key.
// This framing multiplexes bytes, never serializes or inspects shared graphs.
class ConnectionHub {
 public:
  using Factory = std::function<ConnectionAdapters()>;
  explicit ConnectionHub(Factory factory, bool run_worker = true);
  ~ConnectionHub();
  ConnectionHub(ConnectionHub const&) = delete;
  ConnectionHub& operator=(ConnectionHub const&) = delete;
  ConnectionAdapters OpenChannel(std::string id);
  // For an external event loop/deterministic driver; only when
  // run_worker=false.
  void Service(ConnectionTime);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace apptraverse
