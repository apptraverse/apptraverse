#ifndef APPTRAVERSE_RUNTIME_TRACE_H_
#define APPTRAVERSE_RUNTIME_TRACE_H_

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

namespace apptraverse {

// Process-start monotonic clock (steady_clock).
inline std::chrono::steady_clock::time_point& RuntimeTraceOrigin() {
  static auto origin = std::chrono::steady_clock::now();
  return origin;
}

inline void RuntimeTraceReset() {
  RuntimeTraceOrigin() = std::chrono::steady_clock::now();
}

inline double RuntimeElapsedMs() {
  auto const now = std::chrono::steady_clock::now();
  return std::chrono::duration<double, std::milli>(now - RuntimeTraceOrigin())
      .count();
}

struct RuntimeThreadIds {
  std::atomic<std::thread::id> ui{};
  std::atomic<std::thread::id> business{};
  std::atomic<std::thread::id> network{};
};

inline RuntimeThreadIds& RuntimeThreads() {
  static RuntimeThreadIds ids;
  return ids;
}

inline char const* RuntimeCurrentThreadLabel() {
  auto const id = std::this_thread::get_id();
  auto& ids = RuntimeThreads();
  if (id == ids.ui.load(std::memory_order::relaxed)) {
    return "UI";
  }
  if (id == ids.business.load(std::memory_order::relaxed)) {
    return "BUSINESS";
  }
  if (id == ids.network.load(std::memory_order::relaxed)) {
    return "NETWORK";
  }
  if (ids.ui.load(std::memory_order::relaxed) == std::thread::id{}) {
    return "UI";
  }
  return "OTHER";
}

inline std::mutex& RuntimeTraceIoMutex() {
  static std::mutex mu;
  return mu;
}

// TRACE t_ms=<ms> thread=<UI|BUSINESS|NETWORK> event=<name> [detail...]
inline void Trace(std::string_view event, std::string_view detail = {}) {
  std::scoped_lock lock{RuntimeTraceIoMutex()};
  if (detail.empty()) {
    std::printf("TRACE t_ms=%.1f thread=%s event=%.*s\n", RuntimeElapsedMs(),
                RuntimeCurrentThreadLabel(), static_cast<int>(event.size()),
                event.data());
  } else {
    std::printf("TRACE t_ms=%.1f thread=%s event=%.*s %.*s\n",
                RuntimeElapsedMs(), RuntimeCurrentThreadLabel(),
                static_cast<int>(event.size()), event.data(),
                static_cast<int>(detail.size()), detail.data());
  }
  std::fflush(stdout);
}

inline void StartupTrace(std::string_view event,
                         std::string_view detail = {}) {
  std::scoped_lock lock{RuntimeTraceIoMutex()};
  if (detail.empty()) {
    std::printf("STARTUP t_ms=%.1f thread=%s event=%.*s\n", RuntimeElapsedMs(),
                RuntimeCurrentThreadLabel(), static_cast<int>(event.size()),
                event.data());
  } else {
    std::printf("STARTUP t_ms=%.1f thread=%s event=%.*s %.*s\n",
                RuntimeElapsedMs(), RuntimeCurrentThreadLabel(),
                static_cast<int>(event.size()), event.data(),
                static_cast<int>(detail.size()), detail.data());
  }
  std::fflush(stdout);
}

inline bool TraceOnceFlag(std::atomic<bool>& flag) {
  bool expected = false;
  return flag.compare_exchange_strong(expected, true,
                                      std::memory_order::acq_rel);
}

inline std::atomic<bool>& StartupFlagFirstAetherUpdate() {
  static std::atomic<bool> flag{false};
  return flag;
}
inline std::atomic<bool>& StartupFlagFirstAetherUpdateEnd() {
  static std::atomic<bool> flag{false};
  return flag;
}
inline std::atomic<bool>& StartupFlagFirstNetworkWrite() {
  static std::atomic<bool> flag{false};
  return flag;
}
inline std::atomic<bool>& StartupFlagFirstCloudResponse() {
  static std::atomic<bool> flag{false};
  return flag;
}
inline std::atomic<bool>& StartupFlagFirstNetworkEnq() {
  static std::atomic<bool> flag{false};
  return flag;
}
inline std::atomic<bool>& StartupFlagUiOnlinePresented() {
  static std::atomic<bool> flag{false};
  return flag;
}
inline std::atomic<double>& StartupSelectBeginMs() {
  static std::atomic<double> ms{0.0};
  return ms;
}

inline std::mutex& TracePacketKindMutex() {
  static std::mutex mu;
  return mu;
}
inline std::unordered_map<std::uint32_t, std::string>& TracePacketKindMap() {
  static std::unordered_map<std::uint32_t, std::string> map;
  return map;
}
inline void TraceRememberPacketKind(std::uint32_t packet_id,
                                    std::string_view kind) {
  std::scoped_lock lock{TracePacketKindMutex()};
  TracePacketKindMap()[packet_id] = std::string{kind};
}
inline std::string TraceLookupPacketKind(std::uint32_t packet_id) {
  std::scoped_lock lock{TracePacketKindMutex()};
  auto it = TracePacketKindMap().find(packet_id);
  if (it == TracePacketKindMap().end()) {
    return {};
  }
  return it->second;
}

inline void AssertUiThread(char const* where) {
#ifndef NDEBUG
  auto const expected = RuntimeThreads().ui.load(std::memory_order::relaxed);
  if (expected != std::thread::id{} &&
      std::this_thread::get_id() != expected) {
    std::fprintf(stderr, "THREAD_ASSERT_FAIL AssertUiThread at %s\n",
                 where != nullptr ? where : "?");
    std::fflush(stderr);
    assert(false && "AssertUiThread");
  }
#else
  (void)where;
#endif
}
inline void AssertBusinessThread(char const* where) {
#ifndef NDEBUG
  auto const expected =
      RuntimeThreads().business.load(std::memory_order::relaxed);
  if (expected != std::thread::id{} &&
      std::this_thread::get_id() != expected) {
    std::fprintf(stderr, "THREAD_ASSERT_FAIL AssertBusinessThread at %s\n",
                 where != nullptr ? where : "?");
    std::fflush(stderr);
    assert(false && "AssertBusinessThread");
  }
#else
  (void)where;
#endif
}
inline void AssertNetworkThread(char const* where) {
#ifndef NDEBUG
  auto const expected =
      RuntimeThreads().network.load(std::memory_order::relaxed);
  if (expected != std::thread::id{} &&
      std::this_thread::get_id() != expected) {
    std::fprintf(stderr, "THREAD_ASSERT_FAIL AssertNetworkThread at %s\n",
                 where != nullptr ? where : "?");
    std::fflush(stderr);
    assert(false && "AssertNetworkThread");
  }
#else
  (void)where;
#endif
}

}  // namespace apptraverse

// Compatibility aliases used by examples/*.
namespace apptraverse::examples {
using apptraverse::AssertBusinessThread;
using apptraverse::AssertNetworkThread;
using apptraverse::AssertUiThread;
using apptraverse::RuntimeElapsedMs;
using apptraverse::RuntimeThreads;
using apptraverse::RuntimeTraceReset;
using apptraverse::StartupFlagFirstAetherUpdate;
using apptraverse::StartupFlagFirstAetherUpdateEnd;
using apptraverse::StartupFlagFirstCloudResponse;
using apptraverse::StartupFlagFirstNetworkEnq;
using apptraverse::StartupFlagFirstNetworkWrite;
using apptraverse::StartupFlagUiOnlinePresented;
using apptraverse::StartupSelectBeginMs;
using apptraverse::StartupTrace;
using apptraverse::Trace;
using apptraverse::TraceLookupPacketKind;
using apptraverse::TraceOnceFlag;
using apptraverse::TraceRememberPacketKind;

inline double StartupElapsedMs() { return RuntimeElapsedMs(); }
inline void StartupTraceReset() { RuntimeTraceReset(); }
inline auto& StartupThreads() { return RuntimeThreads(); }
inline bool StartupOnceFlag(std::atomic<bool>& flag) {
  return TraceOnceFlag(flag);
}
inline char const* StartupCurrentThreadLabel() {
  return RuntimeCurrentThreadLabel();
}
}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_RUNTIME_TRACE_H_
