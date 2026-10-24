#include "apptraverse/directory_domain_storage.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

#include "apptraverse/noninteractive_crt.h"

#include <fstream>

#include <ios>

#include <set>

#include <string>

#include <vector>

namespace apptraverse {

namespace {

[[noreturn]] void FatalStorageOperation(char const* operation) {
  WriteFatalStderr(operation);
  std::abort();
}

enum class LayerFileReadStatus : std::uint8_t {

  kNotFound,

  kEmpty,

  kOk,

  kReadError,

};

struct LayerFileRead {

  LayerFileReadStatus status{LayerFileReadStatus::kNotFound};

  std::vector<std::uint8_t> bytes;

};

LayerFileRead ReadExistingLayerFile(std::filesystem::path const& path) {

  std::error_code ec;

  if (!std::filesystem::exists(path, ec) || ec) {

    return {LayerFileReadStatus::kNotFound, {}};

  }

  if (!std::filesystem::is_regular_file(path, ec) || ec) {

    return {LayerFileReadStatus::kReadError, {}};

  }

  std::ifstream in{path, std::ios::binary | std::ios::ate};

  if (!in) {

    return {LayerFileReadStatus::kReadError, {}};

  }

  auto const size = static_cast<std::size_t>(in.tellg());

  if (size == 0) {

    return {LayerFileReadStatus::kEmpty, {}};

  }

  in.seekg(0);

  std::vector<std::uint8_t> bytes(size);

  in.read(reinterpret_cast<char*>(bytes.data()),

          static_cast<std::streamsize>(size));

  if (!in) {

    return {LayerFileReadStatus::kReadError, {}};

  }

  return {LayerFileReadStatus::kOk, std::move(bytes)};

}

bool ReplaceLayerFile(std::filesystem::path const& path,

                      std::vector<std::uint8_t> const& bytes) {

  std::error_code ec;

  std::filesystem::create_directories(path.parent_path(), ec);

  if (ec) {

    return false;

  }

  auto const tmp = path.string() + ".tmp";

  std::filesystem::path const tmp_path{tmp};

  {

    std::ofstream out{tmp_path,

                      std::ios::out | std::ios::binary | std::ios::trunc};

    if (!out) {

      return false;

    }

    if (!bytes.empty()) {

      out.write(reinterpret_cast<char const*>(bytes.data()),

                static_cast<std::streamsize>(bytes.size()));

      if (!out) {

        std::filesystem::remove(tmp_path, ec);

        return false;

      }

    }

    out.flush();

    if (!out) {

      std::filesystem::remove(tmp_path, ec);

      return false;

    }

  }

  ec.clear();

  std::filesystem::rename(tmp_path, path, ec);

  if (ec) {

    std::filesystem::remove(tmp_path, ec);

    return false;

  }

  return true;

}

class CompareOnCloseDirectoryStorageWriter final : public ae::IDomainStorageWriter {

 public:

  CompareOnCloseDirectoryStorageWriter(ae::DomainQuery query,

                                       std::filesystem::path path,

                                       DirectoryDomainStorage* owner)

      : query_{std::move(query)},

        path_{std::move(path)},

        owner_{owner} {}

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

    if (owner_ != nullptr) {

      owner_->RecordLayerBytesAccepted(data.size);

    }

    return ae::Ok{ae::seri::good};

  }

 private:

  void CommitIfNeeded() {

    if (committed_ || failed_) {

      return;

    }

    committed_ = true;

    LayerFileRead const existing_read = ReadExistingLayerFile(path_);

    if (existing_read.status == LayerFileReadStatus::kReadError) {

      failed_ = true;

      if (owner_ != nullptr) {

        owner_->RecordLayerCommit(DirectoryDomainStorage::LayerCommitOutcome::kFailed,

                                  0);

      }

      return;

    }

    std::vector<std::uint8_t> const& existing = existing_read.bytes;

    bool const had_file = existing_read.status != LayerFileReadStatus::kNotFound;

    if (had_file && existing == buffer_) {

      if (owner_ != nullptr) {

        owner_->RecordLayerCommit(

            DirectoryDomainStorage::LayerCommitOutcome::kSkippedUnchanged, 0);

      }

      return;

    }

    if (!ReplaceLayerFile(path_, buffer_)) {

      failed_ = true;

      if (owner_ != nullptr) {

        owner_->RecordLayerCommit(DirectoryDomainStorage::LayerCommitOutcome::kFailed,

                                  0);

      }

      return;

    }

    if (owner_ != nullptr) {

      owner_->RecordLayerCommit(DirectoryDomainStorage::LayerCommitOutcome::kWritten,

                                buffer_.size());

    }

  }

  ae::DomainQuery query_;

  std::filesystem::path path_;

  DirectoryDomainStorage* owner_;

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

void DirectoryDomainStorage::BeginSaveSession() {

  save_session_stats_ = {};

  save_session_active_ = true;

  save_session_had_failure_ = false;

  last_layer_commit_ = LayerCommitOutcome::kNone;

}

DirectoryDomainStorageStoreStats DirectoryDomainStorage::SnapshotSaveSessionStats()

    const {

  return save_session_stats_;

}

bool DirectoryDomainStorage::SaveSessionHadFailure() const {

  return save_session_had_failure_;

}

DirectoryDomainStorage::LayerCommitOutcome

DirectoryDomainStorage::TakeLastLayerCommitOutcome() {

  LayerCommitOutcome const out = last_layer_commit_;

  last_layer_commit_ = LayerCommitOutcome::kNone;

  return out;

}

void DirectoryDomainStorage::RecordLayerBytesAccepted(std::size_t bytes) {

  if (save_session_active_) {

    save_session_stats_.layers_bytes_accepted += bytes;

  }

}

void DirectoryDomainStorage::RecordLayerCommit(LayerCommitOutcome outcome,

                                               std::size_t disk_bytes) {

  last_layer_commit_ = outcome;

  if (!save_session_active_) {

    if (outcome == LayerCommitOutcome::kFailed) {

      save_session_had_failure_ = true;

    }

    return;

  }

  ++save_session_stats_.layer_store_calls;

  switch (outcome) {

    case LayerCommitOutcome::kSkippedUnchanged:

      ++save_session_stats_.layers_skipped_unchanged;

      break;

    case LayerCommitOutcome::kWritten:

      ++save_session_stats_.layers_written;

      save_session_stats_.layers_written_bytes += disk_bytes;

      break;

    case LayerCommitOutcome::kFailed:

      ++save_session_stats_.layers_failed_commits;

      save_session_had_failure_ = true;

      break;

    case LayerCommitOutcome::kNone:

      break;

  }

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
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    FatalStorageOperation("fatal: create storage directory failed\n");
  }
}

DirectoryDomainStorage::~DirectoryDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> DirectoryDomainStorage::Store(
    ae::DomainQuery const& query) {
  auto path = DirectoryDomainStorageLayerPath(root_, query);
  return std::make_unique<CompareOnCloseDirectoryStorageWriter>(
      query, std::move(path), this);
}

ae::ClassList DirectoryDomainStorage::Enumerate(ae::ObjId const& obj_id) {
  std::set<std::uint32_t> classes;
  auto const object_dir = root_ / std::to_string(obj_id.id());
  std::error_code ec;
  bool const exists = std::filesystem::exists(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: inspect storage object directory failed\n");
  }
  if (!exists) {
    return {};
  }
  std::filesystem::directory_iterator it{object_dir, ec}, end;
  if (ec) {
    FatalStorageOperation("fatal: enumerate storage object directory failed\n");
  }
  for (; it != end; it.increment(ec)) {
    if (ec) {
      FatalStorageOperation("fatal: advance storage directory iterator failed\n");
    }
    auto const filename = it->path().filename().string();
    std::uint32_t class_id;
    auto const parsed = std::from_chars(filename.data(),
                                        filename.data() + filename.size(), class_id);
    if (parsed.ec != std::errc{} || parsed.ptr != filename.data() + filename.size()) {
      FatalStorageOperation("fatal: invalid stored ClassId directory name\n");
    }
    classes.insert(class_id);
  }
  if (ec) {
    FatalStorageOperation("fatal: advance storage directory iterator failed\n");
  }
  return ae::ClassList{classes.begin(), classes.end()};
}

ae::DomainLoad DirectoryDomainStorage::Load(ae::DomainQuery const& query) {
  auto const object_dir = root_ / std::to_string(query.id.id());
  std::error_code ec;
  bool const exists = std::filesystem::exists(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: inspect stored object failed\n");
  }
  if (!exists) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }
  bool const empty = std::filesystem::is_empty(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: inspect stored object tombstone failed\n");
  }
  if (empty) {
    return {ae::DomainLoadResult::kRemoved, {}};
  }
  auto const path = DirectoryDomainStorageLayerPath(root_, query);
  std::ifstream file{path, std::ios::binary};
  if (!file.good()) {
    return {ae::DomainLoadResult::kEmpty, {}};
  }
  return {ae::DomainLoadResult::kLoaded,
          std::make_unique<DirectoryStorageReader>(std::move(file))};
}

void DirectoryDomainStorage::Remove(ae::ObjId const& obj_id) {
  auto const object_dir = root_ / std::to_string(obj_id.id());
  std::error_code ec;
  std::filesystem::remove_all(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: remove stored object failed\n");
  }
  // An empty object directory is the existing tombstone representation.
  std::filesystem::create_directories(object_dir, ec);
  if (ec) {
    FatalStorageOperation("fatal: create stored object tombstone failed\n");
  }
}

void DirectoryDomainStorage::CleanUp() {
  std::error_code ec;
  std::filesystem::remove_all(root_, ec);
  if (ec) {
    FatalStorageOperation("fatal: clear storage failed\n");
  }
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    FatalStorageOperation("fatal: recreate storage directory failed\n");
  }
}

}  // namespace apptraverse
