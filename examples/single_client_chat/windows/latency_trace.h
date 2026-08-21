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

// Opt-in latency path: POD append only. No console I/O. CSV dump in Flush().
class LatencyTrace {
 public:
  static constexpr std::size_t kMaxRecordsPerRole = 8192;
  static constexpr std::size_t kTextKeyCap = 64;

  enum class Marker : std::uint8_t {
    kEventCommitted = 0,
    kPendingAdded,
    kSessionGenerationChanged,
    kFlushPendingBegin,
    kSyncTransportWrite,
    kP2pWriteCalled,
    kSyncPacketReceived,
    kSyncAckReceived,
    kPendingRemoved,
    // Kept for existing UI / snapshot callers; not required by reconnect bench.
    kUiSendClick,
    kRemoteP2pReceived,
    kSyncEventApplied,
    kUiTranscriptApplied,
    kModelSnapshotLoadBegin,
    kModelSnapshotLoadEnd,
    kModelSnapshotSaveBegin,
    kModelSnapshotSaveEnd,
    // Reconnect / presence transitions (opt-in CSV only).
    kPeerOffline,
    kPeerOnline,
    kReconnectStarted,
    kReconnectCompleted,
  };

  struct Record {
    std::int64_t timestamp_us{0};
    TraceThreadRole role{TraceThreadRole::kUi};
    Marker marker{Marker::kEventCommitted};
    std::uint32_t event_id{0};
    std::uint32_t packet_id{0};
    std::uint32_t peer_id_hash{0};
    bool has_event_id{false};
    bool has_packet_id{false};
    bool has_peer_hash{false};
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

  void Mark(TraceThreadRole role, Marker marker, char const* /*text_key*/ = nullptr,
            std::optional<std::uint32_t> event_id = std::nullopt,
            std::optional<std::uint32_t> packet_id = std::nullopt,
            std::optional<std::uint32_t> peer_hash = std::nullopt) {
    if (!enabled_) {
      return;
    }
    auto& buf = buffers_[static_cast<std::size_t>(role)];
    if (buf.size() >= kMaxRecordsPerRole) {
      return;
    }
    Record rec;
    rec.timestamp_us = MonoMicros();
    rec.role = role;
    rec.marker = marker;
    if (event_id.has_value()) {
      rec.has_event_id = true;
      rec.event_id = *event_id;
    }
    if (packet_id.has_value()) {
      rec.has_packet_id = true;
      rec.packet_id = *packet_id;
    }
    if (peer_hash.has_value()) {
      rec.has_peer_hash = true;
      rec.peer_id_hash = *peer_hash;
    }
    buf.push_back(rec);
  }

  // Map product sync lines → key markers only (no RETRY / SUPPRESS / AUTH).
  void MarkFromProductLine(TraceThreadRole role, std::string const& line) {
    if (!enabled_) {
      return;
    }
    auto const peer = PeerHashFromLine(line);
    if (line.find("CHAT_EVENT_COMMITTED") != std::string::npos) {
      Mark(role, Marker::kEventCommitted, nullptr, ParseU32(line, "event="),
           std::nullopt, peer);
    } else if (line.find("SYNC_PACKET_CREATED kind=event") != std::string::npos) {
      Mark(role, Marker::kPendingAdded, nullptr, ParseU32(line, "event="),
           ParseU32(line, "packet="), peer);
    } else if (line.find("CHAT_PEER_OFFLINE") != std::string::npos) {
      Mark(role, Marker::kPeerOffline, nullptr, std::nullopt, std::nullopt,
           peer);
    } else if (line.find("CHAT_PEER_ONLINE") != std::string::npos ||
               line.find("CHAT_PEER_REJOINED") != std::string::npos) {
      Mark(role, Marker::kPeerOnline, nullptr, std::nullopt, std::nullopt,
           peer);
    } else if (line.find("CHAT_TRANSPORT_SESSION_READY") != std::string::npos) {
      Mark(role, Marker::kSessionGenerationChanged, nullptr, std::nullopt,
           ParseU32(line, "generation="), peer);
    } else if (line.find("CHAT_SYNC_RECONNECT_BEGIN") != std::string::npos) {
      Mark(role, Marker::kReconnectStarted, nullptr, std::nullopt,
           ParseU32(line, "generation="), peer);
    } else if (line.find("CHAT_PENDING_FLUSH_BEGIN") != std::string::npos) {
      Mark(role, Marker::kFlushPendingBegin, nullptr, std::nullopt,
           ParseU32(line, "generation="), peer);
    } else if (line.find("CHAT_SYNC_RECONNECT_END") != std::string::npos) {
      Mark(role, Marker::kReconnectCompleted, nullptr, std::nullopt,
           ParseU32(line, "generation="), peer);
    } else if (line.find("SYNC_TRANSPORT_WRITE") != std::string::npos) {
      Mark(role, Marker::kSyncTransportWrite, nullptr, std::nullopt,
           ParseU32(line, "packet="), peer);
    } else if (line.find("SYNC_PACKET_RECEIVED kind=event") !=
               std::string::npos) {
      Mark(role, Marker::kSyncPacketReceived, nullptr, std::nullopt,
           ParseU32(line, "packet="), peer);
    } else if (line.find("SYNC_ACK_RECEIVED") != std::string::npos) {
      Mark(role, Marker::kSyncAckReceived, nullptr, std::nullopt,
           ParseU32(line, "acknowledged="), peer);
    } else if (line.find("SYNC_PENDING_REMOVED") != std::string::npos) {
      Mark(role, Marker::kPendingRemoved, nullptr, std::nullopt,
           ParseU32(line, "packet="), peer);
    } else if (line.find("SYNC_EVENT_APPLIED") != std::string::npos) {
      Mark(role, Marker::kSyncEventApplied, nullptr, ParseU32(line, "event="),
           ParseU32(line, "packet="), peer);
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

  // One-shot CSV dump after the measured run. Not on the hot path.
  void Flush() {
    if (!enabled_ || path_.empty()) {
      return;
    }
    std::ofstream out(path_, std::ios::out | std::ios::trunc);
    if (!out) {
      return;
    }
    out << "timestamp_us,marker,event_id,packet_id,peer_id_hash,thread\n";
    for (auto const& buf : buffers_) {
      for (auto const& r : buf) {
        out << r.timestamp_us << ',' << MarkerName(r.marker) << ',';
        if (r.has_event_id) {
          out << r.event_id;
        }
        out << ',';
        if (r.has_packet_id) {
          out << r.packet_id;
        }
        out << ',';
        if (r.has_peer_hash) {
          out << r.peer_id_hash;
        }
        out << ',' << RoleName(r.role) << '\n';
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
      case Marker::kEventCommitted:
        return "EVENT_COMMITTED";
      case Marker::kPendingAdded:
        return "PENDING_ADDED";
      case Marker::kSessionGenerationChanged:
        return "SESSION_GENERATION_CHANGED";
      case Marker::kFlushPendingBegin:
        return "FLUSH_PENDING_BEGIN";
      case Marker::kSyncTransportWrite:
        return "SYNC_TRANSPORT_WRITE";
      case Marker::kP2pWriteCalled:
        return "P2P_WRITE_CALLED";
      case Marker::kSyncPacketReceived:
        return "SYNC_PACKET_RECEIVED";
      case Marker::kSyncAckReceived:
        return "SYNC_ACK_RECEIVED";
      case Marker::kPendingRemoved:
        return "PENDING_REMOVED";
      case Marker::kUiSendClick:
        return "UI_SEND_CLICK";
      case Marker::kRemoteP2pReceived:
        return "REMOTE_P2P_RECEIVED";
      case Marker::kSyncEventApplied:
        return "SYNC_EVENT_APPLIED";
      case Marker::kUiTranscriptApplied:
        return "UI_TRANSCRIPT_APPLIED";
      case Marker::kModelSnapshotLoadBegin:
        return "MODEL_SNAPSHOT_LOAD_BEGIN";
      case Marker::kModelSnapshotLoadEnd:
        return "MODEL_SNAPSHOT_LOAD_END";
      case Marker::kModelSnapshotSaveBegin:
        return "MODEL_SNAPSHOT_SAVE_BEGIN";
      case Marker::kModelSnapshotSaveEnd:
        return "MODEL_SNAPSHOT_SAVE_END";
      case Marker::kPeerOffline:
        return "PEER_OFFLINE";
      case Marker::kPeerOnline:
        return "PEER_ONLINE";
      case Marker::kReconnectStarted:
        return "RECONNECT_STARTED";
      case Marker::kReconnectCompleted:
        return "RECONNECT_COMPLETED";
    }
    return "UNKNOWN";
  }

  static std::int64_t MonoMicros() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(
               clock::now().time_since_epoch())
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

  static std::optional<std::uint32_t> PeerHashFromLine(std::string const& line) {
    auto pos = line.find("peer=");
    if (pos == std::string::npos) {
      pos = line.find("peer_uid=");
      if (pos == std::string::npos) {
        return std::nullopt;
      }
      pos += 9;
    } else {
      pos += 5;
    }
    std::uint32_t h = 2166136261u;
    for (; pos < line.size(); ++pos) {
      char const c = line[pos];
      if (c == ' ' || c == '\t') {
        break;
      }
      h ^= static_cast<std::uint8_t>(c);
      h *= 16777619u;
    }
    return h;
  }

  bool enabled_{false};
  std::filesystem::path path_;
  std::array<std::vector<Record>, 3> buffers_;
};

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_LATENCY_TRACE_H_
