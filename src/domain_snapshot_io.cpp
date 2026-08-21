#include "apptraverse/domain_snapshot_io.h"

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace apptraverse {
namespace {

std::mutex g_stats_mu;
DomainSnapshotIoStats g_stats;
std::mutex g_marker_mu;
DomainSnapshotMarkerFn g_marker_sink;

void EmitMarker(std::string const& marker) {
  DomainSnapshotMarkerFn sink;
  {
    std::scoped_lock lock{g_marker_mu};
    sink = g_marker_sink;
  }
  if (sink) {
    sink(marker);
  }
}

bool IsAllDigits(std::string const& s) {
  if (s.empty()) {
    return false;
  }
  for (char ch : s) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

std::vector<std::uint8_t> ReadEntireFile(std::filesystem::path const& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  in.seekg(0, std::ios::end);
  auto const size = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(size);
  if (size > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
  }
  return bytes;
}

void WriteEntireFile(std::filesystem::path const& path,
                     std::vector<std::uint8_t> const& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!bytes.empty()) {
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
}

}  // namespace

DomainSnapshotIoStats& GetDomainSnapshotIoStats() {
  std::scoped_lock lock{g_stats_mu};
  return g_stats;
}

void ResetDomainSnapshotIoStats() {
  std::scoped_lock lock{g_stats_mu};
  g_stats = {};
}

void SetDomainSnapshotMarkerSink(DomainSnapshotMarkerFn sink) {
  std::scoped_lock lock{g_marker_mu};
  g_marker_sink = std::move(sink);
}

void LoadDirectorySnapshot(std::filesystem::path const& directory,
                           ae::RamDomainStorage& ram) {
  EmitMarker("MODEL_SNAPSHOT_LOAD_BEGIN");
  std::uint64_t files = 0;
  std::uint64_t bytes = 0;

  std::error_code ec;
  if (!std::filesystem::exists(directory, ec) || ec) {
    {
      std::scoped_lock lock{g_stats_mu};
      ++g_stats.load_calls;
      g_stats.last_load_files = 0;
      g_stats.last_load_bytes = 0;
    }
    EmitMarker("MODEL_SNAPSHOT_LOAD_FILES=0");
    EmitMarker("MODEL_SNAPSHOT_LOAD_BYTES=0");
    EmitMarker("MODEL_SNAPSHOT_LOAD_END");
    return;
  }

  ram.CleanUp();

  for (auto const& obj_entry :
       std::filesystem::directory_iterator(directory, ec)) {
    if (ec || !obj_entry.is_directory(ec)) {
      continue;
    }
    auto const obj_name = obj_entry.path().filename().string();
    if (!IsAllDigits(obj_name)) {
      continue;
    }
    auto const obj_id = ae::ObjId{static_cast<ae::ObjId::Type>(std::stoul(obj_name))};

    bool has_class_child = false;
    for (auto const& class_entry :
         std::filesystem::directory_iterator(obj_entry.path(), ec)) {
      if (ec || !class_entry.is_directory(ec)) {
        continue;
      }
      auto const class_name = class_entry.path().filename().string();
      if (!IsAllDigits(class_name)) {
        continue;
      }
      has_class_child = true;
      auto const class_id = static_cast<std::uint32_t>(std::stoul(class_name));

      for (auto const& version_entry :
           std::filesystem::directory_iterator(class_entry.path(), ec)) {
        if (ec || !version_entry.is_regular_file(ec)) {
          continue;
        }
        auto const version_name = version_entry.path().filename().string();
        if (!IsAllDigits(version_name)) {
          continue;
        }
        auto const version =
            static_cast<std::uint8_t>(std::stoul(version_name));
        auto data = ReadEntireFile(version_entry.path());
        bytes += data.size();
        ++files;
        ram.SaveData(ae::DomainQuery{obj_id, class_id, version},
                     std::move(data));
      }
    }

    if (!has_class_child) {
      // Empty object directory == removed tombstone.
      ram.Remove(obj_id);
    }
  }

  {
    std::scoped_lock lock{g_stats_mu};
    ++g_stats.load_calls;
    g_stats.last_load_files = files;
    g_stats.last_load_bytes = bytes;
  }
  EmitMarker("MODEL_SNAPSHOT_LOAD_FILES=" + std::to_string(files));
  EmitMarker("MODEL_SNAPSHOT_LOAD_BYTES=" + std::to_string(bytes));
  EmitMarker("MODEL_SNAPSHOT_LOAD_END");
}

void SaveDirectorySnapshot(ae::RamDomainStorage const& ram,
                           std::filesystem::path const& directory) {
  EmitMarker("MODEL_SNAPSHOT_SAVE_BEGIN");
  std::uint64_t files = 0;
  std::uint64_t bytes = 0;

  std::error_code ec;
  std::filesystem::remove_all(directory, ec);
  std::filesystem::create_directories(directory, ec);

  for (auto const& [obj_id, maybe_classes] : ram.state) {
    auto const obj_dir = directory / std::to_string(obj_id.id());
    if (!maybe_classes.has_value()) {
      std::filesystem::create_directories(obj_dir, ec);
      continue;
    }
    for (auto const& [class_id, versions] : *maybe_classes) {
      for (auto const& [version, data] : versions) {
        auto const path = obj_dir / std::to_string(class_id) /
                          std::to_string(static_cast<unsigned>(version));
        WriteEntireFile(path, data);
        ++files;
        bytes += data.size();
      }
    }
  }

  {
    std::scoped_lock lock{g_stats_mu};
    ++g_stats.save_calls;
    g_stats.last_save_files = files;
    g_stats.last_save_bytes = bytes;
  }
  EmitMarker("MODEL_SNAPSHOT_SAVE_FILES=" + std::to_string(files));
  EmitMarker("MODEL_SNAPSHOT_SAVE_BYTES=" + std::to_string(bytes));
  EmitMarker("MODEL_SNAPSHOT_SAVE_END");
}

}  // namespace apptraverse
