#include "apptraverse/single_file_user_storage.h"

#include <array>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>
#include "aether-miscpp/serialization/binary_archive.h"

namespace apptraverse {
namespace {
constexpr std::uint64_t kMagic = 0x3152455355545041ULL;  // APTUSER1
constexpr std::uint32_t kFormat = 1;
using Scopes = std::map<ae::ObjId, std::array<std::uint32_t, 3>>;
}

bool SingleFileUserStorage::LoadFile() {
  std::error_code ec;
  bool const present = std::filesystem::exists(file_, ec);
  if (ec) return false;
  if (!present) {
    state.clear();
    object_scopes.clear();
    return true;
  }
  std::ifstream input{file_, std::ios::binary};
  if (!input) return false;
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  if (input.bad()) return false;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  std::uint64_t magic{};
  std::uint32_t format{};
  ObjClassData loaded;
  Scopes scopes;
  if (archive.Load(magic).IsErr() || magic != kMagic ||
      archive.Load(format).IsErr() || format != kFormat ||
      archive.Load(loaded).IsErr() || archive.Load(scopes).IsErr()) return false;
  std::map<ae::ObjId, ae::ObjectScope> metadata;
  for (auto const& [id, fields] : scopes) {
    if (fields[2] > 1) return false;
    metadata.emplace(id, ae::ObjectScope{ae::ObjId{fields[0]},
        ae::ObjId{fields[1]}, fields[2] != 0});
  }
  state = std::move(loaded);
  object_scopes = std::move(metadata);
  return true;
}

bool SingleFileUserStorage::SaveFile() const {
  Scopes scopes;
  for (auto const& [id, scope] : object_scopes) {
    scopes.emplace(id, std::array<std::uint32_t, 3>{scope.root_id.id(),
        scope.parent_root_id.id(), scope.is_constant ? 1U : 0U});
  }
  std::vector<std::uint8_t> bytes;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (archive.Save(kMagic).IsErr() || archive.Save(kFormat).IsErr() ||
      archive.Save(state).IsErr() || archive.Save(scopes).IsErr()) return false;
  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  if (ec) return false;
  auto temporary = file_;
  temporary += ".tmp";
  {
    std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
    if (!output) return false;
    output.write(reinterpret_cast<char const*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) return false;
    output.close();
    if (!output) return false;
  }
  // filesystem::rename replaces an existing regular file on supported hosts.
  std::filesystem::rename(temporary, file_, ec);
  return !ec;
}
}  // namespace apptraverse
