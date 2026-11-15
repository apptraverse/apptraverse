#pragma once
#include <atomic>
#include <functional>
#include <memory>

#include "apptraverse/connection_state.h"
#include "apptraverse/model_object_proxy.h"
namespace apptraverse {
class NetworkSubscription {
 public:
  virtual ~NetworkSubscription() = default;
};
using NetworkChanged = std::function<void(NetworkObservation)>;
using NetworkObserverFactory =
    std::function<std::unique_ptr<NetworkSubscription>(NetworkChanged)>;
// One owner per application process. Its RAII subscriptions quiesce before
// destruction returns. Callbacks enqueue work; they must not destroy
// themselves.
class ProcessServices {
 public:
  class Subscription {
   public:
    virtual ~Subscription() = default;
  };
  explicit ProcessServices(NetworkObserverFactory factory,
                           bool run_clock = true);
  ~ProcessServices();
  ProcessServices(ProcessServices const&) = delete;
  ProcessServices& operator=(ProcessServices const&) = delete;
  std::unique_ptr<Subscription> Subscribe(
      NetworkChanged, std::function<void(std::int64_t)> clock);
  // Explicit source inputs also make lifecycle/fan-out deterministic in tests.
  void ObserveNetwork(NetworkObservation);
  void ObserveClock(std::int64_t utc_us);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Destroy on the model owner's execution context before unloading the graph.
// Queued notifications are cancelled too; they never resolve an unloaded Node.
class ConnectionModelBinding {
 public:
  ConnectionModelBinding(ProcessServices&, ModelObjectProxy proxy,
                         ae::ObjId model, NetworkChanged adapter_network = {});
  ~ConnectionModelBinding();

 private:
  std::shared_ptr<std::atomic<bool>> active_;
  std::unique_ptr<ProcessServices::Subscription> subscription_;
};
}  // namespace apptraverse
