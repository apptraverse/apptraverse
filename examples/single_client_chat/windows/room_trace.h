#ifndef APPTRAVERSE_ROOM_TRACE_H_
#define APPTRAVERSE_ROOM_TRACE_H_

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "aether/types/uid.h"

#include "aether-miscpp/format/format.h"

namespace apptraverse::examples {

class RoomTrace {
 public:
  void Open(std::filesystem::path const& path, std::string role,
            ae::Uid local_uid) {
    std::scoped_lock lock{mu_};
    role_ = std::move(role);
    local_uid_ = FormatUid(local_uid);
    out_.open(path, std::ios::out | std::ios::trunc);
    enabled_ = out_.is_open();
    if (enabled_) {
      WriteUnlocked("layer=ROOM_TRACE_OPEN result=ok");
    }
  }

  bool enabled() const {
    std::scoped_lock lock{mu_};
    return enabled_;
  }

  void Line(std::string_view fields) {
    std::scoped_lock lock{mu_};
    if (!enabled_) {
      return;
    }
    WriteUnlocked(fields);
  }

  void Event(std::string_view layer, std::string_view peer_uid = {},
             std::string_view direction = {}, std::string_view type = {},
             std::string_view revision = {}, std::string_view phase = {},
             std::string_view bytes = {}, std::string_view result = {}) {
    std::string line = "layer=";
    line.append(layer);
    if (!peer_uid.empty()) {
      line.append(" peer_uid=");
      line.append(peer_uid);
    }
    if (!direction.empty()) {
      line.append(" direction=");
      line.append(direction);
    }
    if (!type.empty()) {
      line.append(" type=");
      line.append(type);
    }
    if (!revision.empty()) {
      line.append(" revision=");
      line.append(revision);
    }
    if (!phase.empty()) {
      line.append(" phase=");
      line.append(phase);
    }
    if (!bytes.empty()) {
      line.append(" bytes=");
      line.append(bytes);
    }
    if (!result.empty()) {
      line.append(" result=");
      line.append(result);
    }
    Line(line);
  }

 private:
  static std::string FormatUid(ae::Uid const& uid) {
    return ae::Format("{}", uid);
  }

  static std::uint64_t MonotonicUs() {
    using clock = std::chrono::steady_clock;
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            clock::now().time_since_epoch())
            .count());
  }

  void WriteUnlocked(std::string_view fields) {
    out_ << MonotonicUs() << " role=" << role_ << " local_uid=" << local_uid_
         << ' ' << fields << '\n';
    out_.flush();
  }

  mutable std::mutex mu_;
  bool enabled_{false};
  std::string role_;
  std::string local_uid_;
  std::ofstream out_;
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_ROOM_TRACE_H_
