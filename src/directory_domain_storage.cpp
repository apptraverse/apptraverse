#include "apptraverse/directory_domain_storage.h"

#include <algorithm>
#include <fstream>
#include <ios>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace apptraverse {
namespace {

DirectoryDomainStorageStoreStats* g_store_stats = nullptr;

std::vector<std::uint8_t> ReadExistingLayerFile(std::filesystem::path const& path) {
  std::ifstream in{path, std::ios::binary | std::ios::ate};
  if (!in) {
    return {};
  }
  auto const size = static_cast<std::size_t>(in.tellg());
  in.seekg(0);
  std::vector<std::uint8_t> bytes(size);
  if (size != 0) {
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in) {
      return {};
    }
  }
  return bytes;
}

bool WriteLayerFile(std::filesystem::path const& path,
                    std::vector<std::uint8_t> const& bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out{path, std::ios::out | std::ios::binary | std::ios::trunc};
  if (!out) {
    return false;
  }
  if (!bytes.empty()) {
    out.write(reinterpret_cast<char const*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(out);
}

void NoteLayerStoreCommit(bool written, std::size_t bytes) {
  if (g_store_stats == nullptr) {
    return;
  }
  ++g_store_stats->layer_store_calls;
  if (written) {
    ++g_store_stats->layers_written;
    g_store_stats->layers_written_bytes += bytes;
  } else {
    ++g_store_stats->layers_skipped_unchanged;
  }
}

class CompareOnCloseDirectoryStorageWriter final : public ae::IDomainStorageWriter {
 public:
  CompareOnCloseDirectoryStorageWriter(ae::DomainQuery query,
                                       std::filesystem::path path)
      : query_{std::move(query)}, path_{std::move(path)} {}

  ~CompareOnCloseDirectoryStorageWriter() override { CommitIfNeeded(); }

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    auto const u_size = static_cast<std::uint32_t>(data.size);
    return Write(ae::seri::DataTag{u_size});
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    if (failed_) {
      return ae::Error{ae::seri::write_error};
    }
    auto const* begin = static_cast<std::uint8_t const*>(data.data);
    buffer_.insert(buffer_.end(), begin, begin + data.size);
    return ae::Ok{ae::seri::good};
  }

 private:
  void CommitIfNeeded() {
    if (committed_ || failed_) {
      return;
    }
    committed_ = true;
    std::error_code ec;
    bool const file_exists =
        std::filesystem::is_regular_file(path_, ec) && !ec;
    std::vector<std::uint8_t> const existing =
        file_exists ? ReadExistingLayerFile(path_) : std::vector<std::uint8_t>{};
    if (file_exists && existing == buffer_) {
      NoteLayerStoreCommit(false, 0);
      return;
    }
    if (!WriteLayerFile(path_, buffer_)) {
      failed_ = true;
      NoteLayerStoreCommit(true, buffer_.size());
      return;
    }
    NoteLayerStoreCommit(true, buffer_.size());
  }

  ae::DomainQuery query_;
  std::filesystem::path path_;
  std::vector<std::uint8_t> buffer_;
  bool committed_{false};
  bool failed_{false};
};

class DirectoryStorageReader final : public ae::IDomainStorageReader {
 public:
  explicit DirectoryStorageReader(std::ifstream&& f) : file_{std::move(f)} {}
  ~DirectoryStorageReader() override { file_.close(); }

  ae::seri::SeriResult Read(ae::seri::SizeReadTag data) override {
    std::uint32_t u_size{};
    TRY_RESULT(Read(ae::seri::DataTag{u_size}));
    data.size = static_cast<std::size_t>(u_size);
    return ae::Ok{ae::seri::good};
  }

  ae::seri::SeriResult Read(ae::seri::DataReadTag data) override {
    if (file_.eof()) {
      return ae::Error{ae::seri::read_eof};
    }
    file_.read(reinterpret_cast<std::ifstream::char_type*>(data.data),
               static_cast<std::streamsize>(data.size));
    if (file_.bad()) {
      return ae::Error{ae::seri::read_error};
    }
    if (file_.gcount() != static_cast<std::streamsize>(data.size)) {
      return ae::Error{file_.eof() ? ae::seri::read_eof : ae::seri::read_error};
    }
    return ae::Ok{ae::seri::good};
  }

 private:
  std::ifstream file_;
};

}  // namespace

void SetDirectoryDomainStorageStoreStats(DirectoryDomainStorageStoreStats* stats) {
  g_store_stats = stats;
}

DirectoryDomainStorageStoreStats TakeDirectoryDomainStorageStoreStats() {
  DirectoryDomainStorageStoreStats out{};
  if (g_store_stats != nullptr) {
    out = *g_store_stats;
    g_store_stats = nullptr;
  }
  return out;
}

std::filesystem::path DirectoryDomainStorageLayerPath(
    std::filesystem::path const& root, ae::DomainQuery const& query) {
  return root / std::to_string(query.id.id()) /
         std::to_string(query.class_id) / std::to_string(query.version);
}

bool DirectoryDomainStorageLayerFileExists(std::filesystem::path const& root,
                                           ae::DomainQuery const& query) {
  std::error_code ec;
  return std::filesystem::is_regular_file(DirectoryDomainStorageLayerPath(root, query),
                                          ec);
}

DirectoryDomainStorage::DirectoryDomainStorage(std::filesystem::path root)
    : root_{std::move(root)} {
  std::filesystem::create_directories(root_);
}

DirectoryDomainStorage::~DirectoryDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> DirectoryDomainStorage::Store(
    ae::DomainQuery const& query) {
  auto version_data_path = DirectoryDomainStorageLayerPath(root_, query);
  return std::make_unique<CompareOnCloseDirectoryStorageWriter>(
      query, std::move(version_data_path));
}

ae::ClassList DirectoryDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<uint32_t> classes;
  auto obj_dir = root_ / std::to_string(obj_id.id());
  auto ec = std::error_code{};
  for (auto const& class_dir :
       std::filesystem::directory_iterator(obj_dir, ec)) {
    auto file_name = class_dir.path().filename().string();
    classes.insert(static_cast<std::uint32_t>(std::stoul(file_name)));
  }
  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad DirectoryDomainStorage::Load(ae::DomainQuery const& query) {
  auto object_dir = root_ / std::to_string(query.id.id());
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }

  auto is_dir_empty = [&]() {
    auto iter = std::filesystem::directory_iterator{object_dir};
    return std::filesystem::begin(iter) == std::filesystem::end(iter);
  };
  if (is_dir_empty()) {
    return {ae::DomainLoadResult::kRemoved, {}};
  }

  auto class_dir = object_dir / std::to_string(query.class_id);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ifstream f(version_data_path, std::ios::in | std::ios::binary);
  if (!f.good()) {
    return ae::DomainLoad{ae::DomainLoadResult::kEmpty, {}};
  }

  return {ae::DomainLoadResult::kLoaded,
          std::make_unique<DirectoryStorageReader>(std::move(f))};
}

void DirectoryDomainStorage::Remove(ae::ObjId const& obj_id) {
  auto object_dir = root_ / std::to_string(obj_id.id());
  auto ec = std::error_code{};
  if (!std::filesystem::exists(object_dir, ec)) {
    std::filesystem::create_directory(object_dir);
    return;
  }
  if (ec) {
    return;
  }

  for (auto const& class_dir :
       std::filesystem::directory_iterator(object_dir, ec)) {
    auto ec2 = std::error_code{};
    std::filesystem::remove_all(class_dir.path(), ec2);
  }
}

void DirectoryDomainStorage::CleanUp() {
  std::filesystem::remove_all(root_);
  std::filesystem::create_directories(root_);
}

namespace {

class InstrumentedStorageWriter final : public ae::IDomainStorageWriter {
 public:
  InstrumentedStorageWriter(ae::DomainQuery query,
                            std::unique_ptr<ae::IDomainStorageWriter> inner,
                            InstrumentedDomainStorageStats& stats, std::mutex& mu,
                            bool layer_existed)
      : query_{std::move(query)},
        inner_{std::move(inner)},
        stats_{stats},
        mu_{mu},
        layer_existed_{layer_existed} {}

  ~InstrumentedStorageWriter() override {
    std::lock_guard<std::mutex> lock{mu_};
    if (bytes_written_ == 0) {
      ++stats_.store_skipped_unchanged_layers;
      return;
    }
    if (layer_existed_) {
      ++stats_.store_overwrite_layers;
    } else {
      ++stats_.store_new_layers;
    }
  }

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    auto const u_size = static_cast<std::uint32_t>(data.size);
    return Write(ae::seri::DataTag{u_size});
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    auto result = inner_->Write(data);
    if (result) {
      std::lock_guard<std::mutex> lock{mu_};
      stats_.store_bytes += data.size;
      bytes_written_ += data.size;
    }
    return result;
  }

 private:
  ae::DomainQuery query_;
  std::unique_ptr<ae::IDomainStorageWriter> inner_;
  InstrumentedDomainStorageStats& stats_;
  std::mutex& mu_;
  bool layer_existed_{false};
  std::size_t bytes_written_{0};
};

class InstrumentedStorageReader final : public ae::IDomainStorageReader {
 public:
  InstrumentedStorageReader(std::unique_ptr<ae::IDomainStorageReader> inner,
                            InstrumentedDomainStorageStats& stats,
                            std::mutex& mu)
      : inner_{std::move(inner)}, stats_{stats}, mu_{mu} {}

  ae::seri::SeriResult Read(ae::seri::SizeReadTag data) override {
    std::uint32_t u_size{};
    auto const size_result = Read(ae::seri::DataTag{u_size});
    if (!size_result) {
      return size_result;
    }
    data.size = static_cast<std::size_t>(u_size);
    return ae::Ok{ae::seri::good};
  }

  ae::seri::SeriResult Read(ae::seri::DataReadTag data) override {
    auto result = inner_->Read(data);
    if (result) {
      std::lock_guard<std::mutex> lock{mu_};
      stats_.load_bytes += data.size;
    }
    return result;
  }

 private:
  std::unique_ptr<ae::IDomainStorageReader> inner_;
  InstrumentedDomainStorageStats& stats_;
  std::mutex& mu_;
};

bool LayerVersionExistsOnDisk(std::filesystem::path const* layer_root,
                              ae::IDomainStorage& storage,
                              ae::DomainQuery const& query) {
  if (layer_root != nullptr) {
    return DirectoryDomainStorageLayerFileExists(*layer_root, query);
  }
  ae::ClassList const classes = storage.Enumerate(query.id);
  return std::find(classes.begin(), classes.end(), query.class_id) !=
         classes.end();
}

}  // namespace

InstrumentedDomainStorage::InstrumentedDomainStorage(ae::IDomainStorage& inner)
    : inner_{inner} {}

InstrumentedDomainStorage::InstrumentedDomainStorage(
    ae::IDomainStorage& inner, std::filesystem::path const& layer_root)
    : inner_{inner}, layer_root_{&layer_root} {}

std::unique_ptr<ae::IDomainStorageWriter> InstrumentedDomainStorage::Store(
    ae::DomainQuery const& query) {
  bool const existed = LayerVersionExistsOnDisk(layer_root_, inner_, query);
  auto inner_writer = inner_.Store(query);
  if (!inner_writer) {
    return {};
  }
  {
    std::lock_guard<std::mutex> lock{mu_};
    ++stats_.store_calls;
    stats_.store_obj_ids.insert(query.id.id());
  }
  return std::make_unique<InstrumentedStorageWriter>(
      query, std::move(inner_writer), stats_, mu_, existed);
}

ae::ClassList InstrumentedDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  return inner_.Enumerate(obj_id);
}

ae::DomainLoad InstrumentedDomainStorage::Load(ae::DomainQuery const& query) {
  ae::DomainLoad load = inner_.Load(query);
  if (load.result == ae::DomainLoadResult::kLoaded && load.reader) {
    {
      std::lock_guard<std::mutex> lock{mu_};
      ++stats_.load_calls;
    }
    load.reader = std::make_unique<InstrumentedStorageReader>(
        std::move(load.reader), stats_, mu_);
  }
  return load;
}

void InstrumentedDomainStorage::Remove(ae::ObjId const& obj_id) {
  inner_.Remove(obj_id);
}

void InstrumentedDomainStorage::CleanUp() { inner_.CleanUp(); }

InstrumentedDomainStorageStats InstrumentedDomainStorage::SnapshotStats() const {
  std::lock_guard<std::mutex> lock{mu_};
  return stats_;
}

void LogInstrumentedDomainStorageStats(InstrumentedDomainStorageStats const& stats) {
  std::cout << "InstrumentedDomainStorage store_calls=" << stats.store_calls
            << " load_calls=" << stats.load_calls << " store_bytes="
            << stats.store_bytes << " load_bytes=" << stats.load_bytes
            << " store_obj_ids=" << stats.store_obj_ids.size()
            << " store_new_layers=" << stats.store_new_layers
            << " store_overwrite_layers=" << stats.store_overwrite_layers
            << " store_skipped_unchanged_layers="
            << stats.store_skipped_unchanged_layers << '\n';
}

}  // namespace apptraverse
