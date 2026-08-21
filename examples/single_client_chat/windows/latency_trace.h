#ifndef APPTRAVERSE_EXAMPLES_LATENCY_TRACE_H_
#define APPTRAVERSE_EXAMPLES_LATENCY_TRACE_H_

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse::examples {

enum class TraceThreadRole : std::uint8_t { kUi, kBusiness, kNetwork };

// Opt-in, off by default. Critical path only appends POD records into
// preallocated per-role buffers. JSON/file I/O happens only in Flush().
class LatencyTrace {
 public:
  static constexpr std::size_t kMaxRecordsPerRole = 4096;
  static constexpr std::size_t kTextKeyCap = 64;

  enum class Marker : std::uint8_t {
    kUiSendClick = 0,
    kEventCommitted,
    kP2pWriteCalled,
    kRemoteP2pReceived,
    kSyncEventApplied,
    kUiTranscriptApplied,
    kSyncPendingRemoved,
    kModelSnapshotLoadBegin,
    kModelSnapshotLoadEnd,
    kModelSnapshotSaveBegin,
    kModelSnapshotSaveEnd,
  };

  struct Record {
    std::int64_t mono_us{0};
    std::int64_t utc_us{0};
    TraceThreadRole role{TraceThreadRole::kUi};
    Marker marker{Marker::kUiSendClick};
    char text_key[kTextKeyCap]{};
    std::uint32_t event_id{0};
    std::uint32_t packet_id{0};
    bool has_event_id{false};
    bool has_packet_id{false};
  };

  void Open(std::filesystem::path path) {
    path_ = std::move(path);
    enabled_ = !path_.empty();
    for (auto& buf : buffers_) {
      buf.clear();
      buf.reserve(kMaxRecordsPerRole);
    }
  }

  bool enabled() const { return enabled_; }

  void Mark(TraceThreadRole role, Marker marker, char const* text_key = nullptr,
            std::optional<std::uint32_t> event_id = std::nullopt,
            std::optional<std::uint32_t> packet_id = std::nullopt) {
    if (!enabled_) {
      return;
    }
    auto& buf = buffers_[static_cast<std::size_t>(role)];
    if (buf.size() >= kMaxRecordsPerRole) {
      return;
    }
    Record rec;
    rec.mono_us = MonoMicros();
    rec.utc_us = UtcMicros();
    rec.role = role;
    rec.marker = marker;
    if (text_key != nullptr && text_key[0] != '\0') {
      std::strncpy(rec.text_key, text_key, kTextKeyCap - 1);
      rec.text_key[kTextKeyCap - 1] = '\0';
    }
    if (event_id.has_value()) {
      rec.has_event_id = true;
      rec.event_id = *event_id;
    }
    if (packet_id.has_value()) {
      rec.has_packet_id = true;
      rec.packet_id = *packet_id;
    }
    buf.push_back(rec);
  }

  // Convert optional product Trace line markers into latency records.
  void MarkFromProductLine(TraceThreadRole role, std::string const& line) {
    if (!enabled_) {
      return;
    }
    if (line.find("SYNC_EVENT_APPLIED") != std::string::npos) {
      Mark(role, Marker::kSyncEventApplied, nullptr, ParseU32(line, "event="),
           ParseU32(line, "packet="));
    } else if (line.find("SYNC_PENDING_REMOVED") != std::string::npos) {
      Mark(role, Marker::kSyncPendingRemoved, nullptr, std::nullopt,
           ParseU32(line, "packet="));
    }
  }

  void MarkSnapshot(char const* marker_name) {
    if (!enabled_ || marker_name == nullptr) {
      return;
    }
    if (std::strcmp(marker_name, "MODEL_SNAPSHOT_LOAD_BEGIN") == 0) {
      Mark(TraceThreadRole::kBusiness, Marker::kModelSnapshotLoadBegin);
    } else if (std::strcmp(marker_name, "MODEL_SNAPSHOT_LOAD_END") == 0) {
      Mark(TraceThreadRole::kBusiness, Marker::kModelSnapshotLoadEnd);
    } else if (std::strcmp(marker_name, "MODEL_SNAPSHOT_SAVE_BEGIN") == 0) {
      Mark(TraceThreadRole::kBusiness, Marker::kModelSnapshotSaveBegin);
    } else if (std::strcmp(marker_name, "MODEL_SNAPSHOT_SAVE_END") == 0) {
      Mark(TraceThreadRole::kBusiness, Marker::kModelSnapshotSaveEnd);
    }
  }

  void Flush() {
    if (!enabled_ || path_.empty()) {
      return;
    }
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
      return;
    }
    auto const pid = static_cast<std::uint32_t>(::GetCurrentProcessId());
    for (auto const& buf : buffers_) {
      for (auto const& r : buf) {
        out << "{\"mono_us\":" << r.mono_us << ",\"utc_us\":" << r.utc_us
            << ",\"pid\":" << pid << ",\"thread\":\"" << RoleName(r.role)
            << "\",\"marker\":\"" << MarkerName(r.marker) << "\"";
        if (r.text_key[0] != '\0') {
          out << ",\"text_key\":\"" << Escape(r.text_key) << "\"";
        }
        if (r.has_event_id) {
          out << ",\"event_id\":" << r.event_id;
        }
        if (r.has_packet_id) {
          out << ",\"packet_id\":" << r.packet_id;
        }
        out << "}\n";
      }
    }
    out.flush();
    for (auto& buf : buffers_) {
      buf.clear();
    }
  }

 private:
  static char const* RoleName(TraceThreadRole role) {
    switch (role) {
      case TraceThreadRole::kUi:
        return "ui";
      case TraceThreadRole::kBusiness:
        return "business";
      case TraceThreadRole::kNetwork:
        return "network";
    }
    return "unknown";
  }

  static char const* MarkerName(Marker m) {
    switch (m) {
      case Marker::kUiSendClick:
        return "UI_SEND_CLICK";
      case Marker::kEventCommitted:
        return "EVENT_COMMITTED";
      case Marker::kP2pWriteCalled:
        return "P2P_WRITE_CALLED";
      case Marker::kRemoteP2pReceived:
        return "REMOTE_P2P_RECEIVED";
      case Marker::kSyncEventApplied:
        return "SYNC_EVENT_APPLIED";
      case Marker::kUiTranscriptApplied:
        return "UI_TRANSCRIPT_APPLIED";
      case Marker::kSyncPendingRemoved:
        return "SYNC_PENDING_REMOVED";
      case Marker::kModelSnapshotLoadBegin:
        return "MODEL_SNAPSHOT_LOAD_BEGIN";
      case Marker::kModelSnapshotLoadEnd:
        return "MODEL_SNAPSHOT_LOAD_END";
      case Marker::kModelSnapshotSaveBegin:
        return "MODEL_SNAPSHOT_SAVE_BEGIN";
      case Marker::kModelSnapshotSaveEnd:
        return "MODEL_SNAPSHOT_SAVE_END";
    }
    return "UNKNOWN";
  }

  static std::int64_t MonoMicros() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(
               clock::now().time_since_epoch())
        .count();
  }

  static std::int64_t UtcMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  static std::optional<std::uint32_t> ParseU32(std::string const& line,
                                               char const* key) {
    auto pos = line.find(key);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    pos += std::strlen(key);
    try {
      return static_cast<std::uint32_t>(std::stoul(line.substr(pos)));
    } catch (...) {
      return std::nullopt;
    }
  }

  static std::string Escape(char const* s) {
    std::string out;
    for (; *s; ++s) {
      if (*s == '\\' || *s == '"') {
        out.push_back('\\');
      }
      if (static_cast<unsigned char>(*s) < 0x20) {
        continue;
      }
      out.push_back(*s);
    }
    return out;
  }

  bool enabled_{false};
  std::filesystem::path path_;
  // One buffer per role — writers only touch their own role from that thread.
  std::array<std::vector<Record>, 3> buffers_{};
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_LATENCY_TRACE_H_
