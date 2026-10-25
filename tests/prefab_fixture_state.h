#ifndef APPTRAVERSE_TESTS_PREFAB_FIXTURE_STATE_H_
#define APPTRAVERSE_TESTS_PREFAB_FIXTURE_STATE_H_

#include <cstdint>
#include <filesystem>
#include <optional>

namespace apptraverse::test::prefab_fixture {

struct StateLayout {
  std::filesystem::path user_root;
  std::filesystem::path state_root;
};

StateLayout ResolveStateLayout(std::filesystem::path const& state_dir);

bool WriteActiveReleaseFile(std::filesystem::path const& path,
                            std::uint32_t release);

std::optional<std::uint32_t> ReadActiveReleaseFile(
    std::filesystem::path const& path);

std::uint32_t ActiveReleaseForStateRoot(
    std::filesystem::path const& state_root, std::uint32_t embedded_release);

}  // namespace apptraverse::test::prefab_fixture

#endif  // APPTRAVERSE_TESTS_PREFAB_FIXTURE_STATE_H_
