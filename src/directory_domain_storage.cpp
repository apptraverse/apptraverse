#include "apptraverse/directory_domain_storage.h"

#include <algorithm>
#include <fstream>
#include <ios>
#include <iostream>
#include <set>
#include <string>

namespace apptraverse {
namespace {

class DirectoryStorageWriter final : public ae::IDomainStorageWriter {
 public:
  DirectoryStorageWriter(ae::DomainQuery q, std::ofstream&& f)
      : query_{std::move(q)}, file_{std::move(f)} {}

  ~DirectoryStorageWriter() override { file_.close(); }

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    auto const u_size = static_cast<std::uint32_t>(data.size);
    return Write(ae::seri::DataTag{u_size});
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    file_.write(reinterpret_cast<std::ofstream::char_type const*>(data.data),
                static_cast<std::streamsize>(data.size));
    if (file_.fail()) {
      return ae::Error{ae::seri::write_error};
    }
    return ae::Ok{ae::seri::good};
  }

 private:
  ae::DomainQuery query_;
  std::ofstream file_;
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

DirectoryDomainStorage::DirectoryDomainStorage(std::filesystem::path root)
    : root_{std::move(root)} {
  std::filesystem::create_directories(root_);
}

DirectoryDomainStorage::~DirectoryDomainStorage() = default;

std::unique_ptr<ae::IDomainStorageWriter> DirectoryDomainStorage::Store(
    ae::DomainQuery const& query) {
  auto class_dir =
      root_ / std::to_string(query.id.id()) / std::to_string(query.class_id);
  std::filesystem::create_directories(class_dir);
  auto version_data_path = class_dir / std::to_string(query.version);
  std::ofstream f(version_data_path,
                  std::ios::out | std::ios::binary | std::ios::trunc);
  return std::make_unique<DirectoryStorageWriter>(query, std::move(f));
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
                            InstrumentedDomainStorageStats& stats, std::mutex& mu)
      : query_{std::move(query)},
        inner_{std::move(inner)},
        stats_{stats},
        mu_{mu} {}

  ae::seri::SeriResult Write(ae::seri::SizeWriteTag data) override {
    auto const u_size = static_cast<std::uint32_t>(data.size);
    return Write(ae::seri::DataTag{u_size});
  }

  ae::seri::SeriResult Write(ae::seri::DataWriteTag data) override {
    auto result = inner_->Write(data);
    if (result) {
      std::lock_guard<std::mutex> lock{mu_};
      stats_.store_bytes += data.size;
    }
    return result;
  }

 private:
  ae::DomainQuery query_;
  std::unique_ptr<ae::IDomainStorageWriter> inner_;
  InstrumentedDomainStorageStats& stats_;
  std::mutex& mu_;
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

bool LayerVersionExists(ae::IDomainStorage& storage, ae::DomainQuery const& query) {
  ae::ClassList const classes = storage.Enumerate(query.id);
  if (std::find(classes.begin(), classes.end(), query.class_id) == classes.end()) {
    return false;
  }
  ae::DomainLoad const load = storage.Load(query);
  return load.result == ae::DomainLoadResult::kLoaded;
}

}  // namespace

InstrumentedDomainStorage::InstrumentedDomainStorage(ae::IDomainStorage& inner)
    : inner_{inner} {}

std::unique_ptr<ae::IDomainStorageWriter> InstrumentedDomainStorage::Store(
    ae::DomainQuery const& query) {
  bool const existed = LayerVersionExists(inner_, query);
  auto inner_writer = inner_.Store(query);
  if (!inner_writer) {
    return {};
  }
  {
    std::lock_guard<std::mutex> lock{mu_};
    ++stats_.store_calls;
    stats_.store_obj_ids.insert(query.id.id());
    if (existed) {
      ++stats_.store_overwrite_layers;
    } else {
      ++stats_.store_new_layers;
    }
  }
  return std::make_unique<InstrumentedStorageWriter>(
      query, std::move(inner_writer), stats_, mu_);
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
            << '\n';
}

}  // namespace apptraverse
