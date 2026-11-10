#include "apptraverse/single_file_user_storage.h"

#include <array>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>
#include "aether-miscpp/serialization/binary_archive.h"

namespace apptraverse {
namespace {
using Scopes = std::map<ae::ObjId, std::array<std::uint32_t, 3>>;
}

bool LoadUserStorageFile(ae::RamDomainStorage& storage, std::filesystem::path const& profile) {
  auto const file_ = profile / "user.bin";
  errno = 0;
  std::ifstream input{file_, std::ios::binary};
  if (!input && errno == ENOENT) {
    storage.state.clear();
    storage.object_scopes.clear();
    return true;
  }
  if (!input) return false;
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  if (input.bad()) return false;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  ae::RamDomainStorage::ObjClassData loaded;
  Scopes scopes;
  if (archive.Load(loaded).IsErr() || archive.Load(scopes).IsErr()) return false;
  std::map<ae::ObjId, ae::ObjectScope> metadata;
  for (auto const& [id, fields] : scopes) {
    metadata.emplace(id, ae::ObjectScope{ae::ObjId{fields[0]},
        ae::ObjId{fields[1]}, fields[2] != 0});
  }
  storage.state = std::move(loaded);
  storage.object_scopes = std::move(metadata);
  return true;
}

bool SaveUserStorageFile(ae::RamDomainStorage const& storage, std::filesystem::path const& profile) {
  auto const file_ = profile / "user.bin";
  Scopes scopes;
  for (auto const& [id, scope] : storage.object_scopes) {
    scopes.emplace(id, std::array<std::uint32_t, 3>{scope.root_id.id(),
        scope.parent_root_id.id(), scope.is_constant ? 1U : 0U});
  }
  std::vector<std::uint8_t> bytes;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (archive.Save(storage.state).IsErr() || archive.Save(scopes).IsErr()) return false;
  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  if (ec) return false;
  {
    std::ofstream output{file_, std::ios::binary | std::ios::trunc};
    if (!output) return false;
    output.write(reinterpret_cast<char const*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) return false;
    output.close();
    if (!output) return false;
  }
  return true;
}
}  // namespace apptraverse
