#include "apptraverse/platform/windows/win_network_observer.h"

#include <netlistmgr.h>
#include <ocidl.h>
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "apptraverse/platform/windows/win32_fatal.h"
namespace apptraverse::windows {
namespace {
void Require(HRESULT hr, char const* operation) {
  if (FAILED(hr)) FatalWin32(operation, static_cast<DWORD>(hr));
}
NetworkObservation Observation(NLM_CONNECTIVITY flags) {
  return {flags == NLM_CONNECTIVITY_DISCONNECTED ? Reachability::Unavailable
                                                 : Reachability::Available,
          (flags &
           (NLM_CONNECTIVITY_IPV4_INTERNET | NLM_CONNECTIVITY_IPV6_INTERNET))
              ? Reachability::Available
              : Reachability::Unavailable};
}
class Sink final : public INetworkListManagerEvents {
  std::atomic<ULONG> references_{1};
  std::mutex mutex_;
  NetworkChanged changed_;
  bool received_{false};
  NetworkObservation last_;

 public:
  explicit Sink(NetworkChanged changed) : changed_{std::move(changed)} {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != __uuidof(INetworkListManagerEvents))
      return E_NOINTERFACE;
    *out = static_cast<INetworkListManagerEvents*>(this);
    AddRef();
    return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    auto n = --references_;
    if (!n) delete this;
    return n;
  }
  void Emit(NLM_CONNECTIVITY flags, bool initial = false) {
    std::lock_guard lock{mutex_};
    auto next = Observation(flags);
    if (!changed_ || (initial && received_) || (received_ && last_ == next))
      return;
    received_ = true;
    last_ = next;
    changed_(next);
  }
  HRESULT STDMETHODCALLTYPE
  ConnectivityChanged(NLM_CONNECTIVITY flags) override {
    Emit(flags);
    return S_OK;
  }
  void Stop() {
    std::lock_guard lock{mutex_};
    changed_ = {};
  }
};
class Observer final : public NetworkSubscription {
  HANDLE stop_;
  std::thread thread_;

 public:
  explicit Observer(NetworkChanged changed)
      : stop_{CreateEventW(nullptr, TRUE, FALSE, nullptr)} {
    if (!stop_)
      FatalWin32("Create network observer stop event", GetLastError());
    thread_ = std::thread([this, changed = std::move(changed)] {
      Require(CoInitializeEx(nullptr, COINIT_MULTITHREADED),
              "Initialize network COM");
      INetworkListManager* manager{};
      IConnectionPointContainer* container{};
      IConnectionPoint* point{};
      DWORD cookie{};
      Require(CoCreateInstance(CLSID_NetworkListManager, nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&manager)),
              "Create network list manager");
      Require(manager->QueryInterface(IID_PPV_ARGS(&container)),
              "Get network event container");
      Require(container->FindConnectionPoint(
                  __uuidof(INetworkListManagerEvents), &point),
              "Get network connection point");
      auto* sink = new Sink(changed);
      Require(point->Advise(sink, &cookie), "Subscribe network events");
      NLM_CONNECTIVITY initial{};
      Require(manager->GetConnectivity(&initial), "Read network state");
      sink->Emit(initial, true);
      if (WaitForSingleObject(stop_, INFINITE) != WAIT_OBJECT_0)
        FatalWin32("Wait network observer", GetLastError());
      sink->Stop();
      Require(point->Unadvise(cookie), "Unsubscribe network events");
      sink->Release();
      point->Release();
      container->Release();
      manager->Release();
      CoUninitialize();
    });
  }
  ~Observer() override {
    if (!SetEvent(stop_)) FatalWin32("Stop network observer", GetLastError());
    thread_.join();
    if (!CloseHandle(stop_))
      FatalWin32("Close network observer", GetLastError());
  }
};
}  // namespace
std::unique_ptr<NetworkSubscription> WatchNetwork(NetworkChanged changed) {
  return std::make_unique<Observer>(std::move(changed));
}
}  // namespace apptraverse::windows
