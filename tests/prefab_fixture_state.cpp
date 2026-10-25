#include "prefab_fixture_state.h"

#include <fstream>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

namespace apptraverse::test::prefab_fixture {

StateLayout ResolveStateLayout(std::filesystem::path const& state_dir) {
  std::filesystem::path const user_candidate = state_dir / "user";
  if (std::filesystem::is_directory(user_candidate)) {
    return StateLayout{.user_root = user_candidate, .state_root = state_dir};
  }
  if (std::filesystem::exists(state_dir / "active-release")) {
    return StateLayout{.user_root = user_candidate, .state_root = state_dir};
  }
  if (std::filesystem::is_directory(state_dir)) {
    for (auto const& entry : std::filesystem::directory_iterator(state_dir)) {
      if (!entry.is_directory()) {
        continue;
      }
      if (std::filesystem::exists(entry.path() / "supply.bin")) {
        return StateLayout{.user_root = user_candidate, .state_root = state_dir};
      }
    }
  }
  return StateLayout{.user_root = state_dir, .state_root = {}};
}

bool WriteActiveReleaseFile(std::filesystem::path const& path,
                            std::uint32_t release) {
  std::vector<std::uint8_t> bytes;
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  if (!archive.Save(release).IsOk()) {
    return false;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<char const*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

std::optional<std::uint32_t> ReadActiveReleaseFile(
    std::filesystem::path const& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  in.seekg(0, std::ios::end);
  auto const size = in.tellg();
  if (size < 0) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  in.seekg(0, std::ios::beg);
  in.read(reinterpret_cast<char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  if (!in) {
    return std::nullopt;
  }
  std::vector<std::uint8_t> copy = bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{std::move(buffer)};
  std::uint32_t release{};
  if (archive.Load(release).IsErr()) {
    return std::nullopt;
  }
  return release;
}

std::uint32_t ActiveReleaseForStateRoot(
    std::filesystem::path const& state_root, std::uint32_t embedded_release) {
  if (state_root.empty()) {
    return embedded_release;
  }
  auto const path = state_root / "active-release";
  if (!std::filesystem::exists(path)) {
    return embedded_release;
  }
  auto const release = ReadActiveReleaseFile(path);
  if (!release) {
    return embedded_release;
  }
  return *release;
}

}  // namespace apptraverse::test::prefab_fixture
