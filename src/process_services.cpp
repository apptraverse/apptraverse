#include "apptraverse/process_services.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

#include "apptraverse/connection_runtime.h"
namespace apptraverse {
struct ProcessServices::Impl {
  struct Slot {
    std::mutex mutex;
    NetworkChanged network;
    std::function<void(std::int64_t)> clock;
    std::uint64_t network_sequence{0}, clock_sequence{0};
    void Network(NetworkObservation v, std::uint64_t seq) {
      std::lock_guard lock{mutex};
      if (network && seq > network_sequence) {
        network_sequence = seq;
        network(v);
      }
    }
    void Clock(std::int64_t v, std::uint64_t seq) {
      std::lock_guard lock{mutex};
      if (clock && seq > clock_sequence) {
        clock_sequence = seq;
        clock(v);
      }
    }
  };
  struct Token final : Subscription {
    std::shared_ptr<Slot> slot;
    explicit Token(std::shared_ptr<Slot> s) : slot{std::move(s)} {}
    ~Token() {
      std::lock_guard lock{slot->mutex};
      slot->network = {};
      slot->clock = {};
    }
  };
  std::mutex mutex;
  std::vector<std::weak_ptr<Slot>> slots;
  NetworkObservation network;
  std::int64_t utc{0};
  std::uint64_t network_sequence{1}, clock_sequence{1};
  std::unique_ptr<NetworkSubscription> observer;
  std::thread clock;
  std::atomic<bool> stop{false};
  std::mutex wake_mutex;
  std::condition_variable wake;
  std::vector<std::shared_ptr<Slot>> Live() {
    std::vector<std::shared_ptr<Slot>> result;
    std::erase_if(slots, [&](auto const& w) {
      if (auto s = w.lock()) {
        result.push_back(s);
        return false;
      }
      return true;
    });
    return result;
  }
};
ProcessServices::ProcessServices(NetworkObserverFactory factory, bool run_clock)
    : impl_{std::make_unique<Impl>()} {
  if (factory)
    impl_->observer = factory([this](auto value) { ObserveNetwork(value); });
  if (run_clock)
    impl_->clock = std::thread([this] {
      while (!impl_->stop.load()) {
        ObserveClock(ReadConnectionTime().utc_us / 1000000 * 1000000);
        std::unique_lock lock{impl_->wake_mutex};
        impl_->wake.wait_for(lock, std::chrono::seconds{1},
                             [this] { return impl_->stop.load(); });
      }
    });
}
ProcessServices::~ProcessServices() {
  impl_->stop.store(true);
  impl_->wake.notify_all();
  if (impl_->clock.joinable()) impl_->clock.join();
  impl_->observer.reset();
}
std::unique_ptr<ProcessServices::Subscription> ProcessServices::Subscribe(
    NetworkChanged net, std::function<void(std::int64_t)> clock) {
  auto slot = std::make_shared<Impl::Slot>();
  slot->network = std::move(net);
  slot->clock = std::move(clock);
  NetworkObservation n;
  std::int64_t utc;
  std::uint64_t ns, cs;
  {
    std::lock_guard lock{impl_->mutex};
    impl_->slots.push_back(slot);
    n = impl_->network;
    utc = impl_->utc;
    ns = impl_->network_sequence;
    cs = impl_->clock_sequence;
  }
  slot->Network(n, ns);
  slot->Clock(utc, cs);
  return std::make_unique<Impl::Token>(slot);
}
void ProcessServices::ObserveNetwork(NetworkObservation v) {
  std::vector<std::shared_ptr<Impl::Slot>> slots;
  std::uint64_t seq;
  {
    std::lock_guard lock{impl_->mutex};
    if (impl_->network == v) return;
    impl_->network = v;
    seq = ++impl_->network_sequence;
    slots = impl_->Live();
  }
  for (auto const& s : slots) s->Network(v, seq);
}
void ProcessServices::ObserveClock(std::int64_t v) {
  std::vector<std::shared_ptr<Impl::Slot>> slots;
  std::uint64_t seq;
  {
    std::lock_guard lock{impl_->mutex};
    if (impl_->utc == v) return;
    impl_->utc = v;
    seq = ++impl_->clock_sequence;
    slots = impl_->Live();
  }
  for (auto const& s : slots) s->Clock(v, seq);
}
ConnectionModelBinding::ConnectionModelBinding(ProcessServices& services,
                                               ModelObjectProxy proxy,
                                               ae::ObjId model,
                                               NetworkChanged adapter)
    : active_{std::make_shared<std::atomic<bool>>(true)} {
  std::weak_ptr<std::atomic<bool>> alive = active_;
  subscription_ = services.Subscribe(
      [proxy, model, alive, adapter = std::move(adapter)](auto v) mutable {
        if (adapter) adapter(v);
        proxy.EnqueueModelWork([model, alive, v](ae::Domain& domain) {
          auto token = alive.lock();
          if (!token || !token->load()) return;
          auto object = domain.Find(model);
          assert(object && "live connection model must exist");
          object.as<ConnectionState>()->ObserveNetwork(v);
        });
      },
      [proxy, model, alive](auto utc) mutable {
        proxy.EnqueueModelWork([model, alive, utc](ae::Domain& domain) {
          auto token = alive.lock();
          if (!token || !token->load()) return;
          auto object = domain.Find(model);
          assert(object && "live connection model must exist");
          object.as<ConnectionState>()->ObserveClock(utc);
        });
      });
}
ConnectionModelBinding::~ConnectionModelBinding() {
  active_->store(false);
  subscription_.reset();
}
}  // namespace apptraverse
