#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_JOIN_DELIVERY_TRACE_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_JOIN_DELIVERY_TRACE_H_

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace apptraverse::example::chat_demo {
namespace join_delivery_trace_internal {

inline std::mutex& Mu() {
  static std::mutex mu;
  return mu;
}

inline std::string& Path() {
  static std::string path;
  return path;
}

inline std::uint64_t MonoMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

inline std::uint64_t HashBytes(std::uint8_t const* data, std::size_t n) {
  std::uint64_t h = 14695981039346656037ull;
  for (std::size_t i = 0; i < n; ++i) {
    h ^= data[i];
    h *= 1099511628211ull;
  }
  return h;
}

inline unsigned long long Pid() {
#if defined(_WIN32)
  return static_cast<unsigned long long>(::GetCurrentProcessId());
#else
  return static_cast<unsigned long long>(::getpid());
#endif
}

}  // namespace join_delivery_trace_internal

// Opt-in: set path explicitly, or via APPTRAVERSE_JOIN_TRACE env.
inline void EnableJoinDeliveryTrace(std::string path) {
  std::lock_guard<std::mutex> lock{join_delivery_trace_internal::Mu()};
  join_delivery_trace_internal::Path() = std::move(path);
}

inline void EnableJoinDeliveryTraceFromEnv() {
  char const* env = std::getenv("APPTRAVERSE_JOIN_TRACE");
  if (env != nullptr && env[0] != '\0') {
    EnableJoinDeliveryTrace(env);
  }
}

inline bool JoinDeliveryTraceEnabled() {
  std::lock_guard<std::mutex> lock{join_delivery_trace_internal::Mu()};
  return !join_delivery_trace_internal::Path().empty();
}

inline void JoinTrace(std::string_view stage, std::string_view role,
                      std::string_view detail,
                      std::uint64_t network_epoch = 0,
                      std::string_view src = {},
                      std::string_view dst = {},
                      std::string_view attempt = {},
                      std::string_view room = {},
                      std::size_t bytes = 0,
                      std::uint64_t byte_hash = 0,
                      std::string_view reason = {}) {
  std::lock_guard<std::mutex> lock{join_delivery_trace_internal::Mu()};
  auto const& path = join_delivery_trace_internal::Path();
  if (path.empty()) {
    return;
  }
  std::ofstream out(path, std::ios::app | std::ios::binary);
  if (!out) {
    return;
  }
  std::ostringstream tid;
  tid << std::this_thread::get_id();
  out << "pid=" << join_delivery_trace_internal::Pid()
      << " t_ms=" << join_delivery_trace_internal::MonoMs()
      << " tid=" << tid.str()
      << " epoch=" << network_epoch
      << " role=" << role
      << " stage=" << stage;
  if (!src.empty()) {
    out << " src=" << src;
  }
  if (!dst.empty()) {
    out << " dst=" << dst;
  }
  if (!attempt.empty()) {
    out << " attempt=" << attempt;
  }
  if (!room.empty()) {
    out << " room=" << room;
  }
  if (bytes != 0) {
    out << " bytes=" << bytes;
  }
  if (byte_hash != 0) {
    out << " hash=0x" << std::hex << byte_hash << std::dec;
  }
  if (!reason.empty()) {
    out << " reason=" << reason;
  }
  if (!detail.empty()) {
    out << " detail=" << detail;
  }
  out << '\n';
}

inline std::uint64_t JoinTraceHash(std::vector<std::uint8_t> const& bytes) {
  if (bytes.empty()) {
    return 0;
  }
  return join_delivery_trace_internal::HashBytes(bytes.data(), bytes.size());
}

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_JOIN_DELIVERY_TRACE_H_
