#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/overlay_domain_storage.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK " #x << " line " << __LINE__ << '\n'; std::exit(1); } } while(false)

namespace {
std::vector<std::uint8_t> ReadBytes(std::filesystem::path const& file) {
  std::ifstream input{file, std::ios::binary};
  CHECK(input);
  std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>{input}, {}};
  CHECK(!input.bad());
  return bytes;
}

void CheckSnapshot(ae::RamDomainStorage& user, ae::ObjectData const& value) {
  ae::ObjId const object{101}, removed{102};
  CHECK(user.state.at(object)->at(301).at(0) == value);
  CHECK(user.state.at(object)->at(301).at(7) == ae::ObjectData({8, 9}));
  CHECK(user.ReadObjectScope(object) ==
        (ae::ObjectScope{object, ae::ObjId{99}, true}));
  CHECK(user.IsConstantObject(object));
  CHECK(user.ReadObjectScope(ae::ObjId{103}) == ae::ObjectScope{});
  CHECK(!user.ReadObjectScope(ae::ObjId{104}).has_value());
  ae::RamDomainStorage backing;
  backing.SaveData(ae::DomainQuery{removed, 1, 0}, {4});
  apptraverse::OverlayDomainStorage overlay{user, backing};
  CHECK(overlay.Load(ae::DomainQuery{removed, 1, 0}).result ==
        ae::DomainLoadResult::kRemoved);
}
}  // namespace

int main() {
  using apptraverse::DeserializeUserStorage;
  using apptraverse::LoadUserStorageFile;
  using apptraverse::SaveUserStorageFile;
  using apptraverse::SerializeUserStorage;
  auto path = std::filesystem::temp_directory_path() /
      ("single-user-" + std::to_string(ae::ObjId::GenerateUnique().id()));
  ae::ObjId const object{101}, removed{102};
  ae::DomainQuery const q{object, 301, 0};
  ae::ObjectScope const scope{object, ae::ObjId{99}, true};
  {
    ae::RamDomainStorage user; auto const user_profile = path;
    CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
    user.SaveData(q, {1, 2, 3});
    user.SaveData(ae::DomainQuery{object, 301, 7}, {8, 9});
    user.Remove(removed);
    user.object_scopes[object] = scope;
    user.object_scopes[ae::ObjId{103}] = {}; // explicit reset, not missing
    std::vector<std::uint8_t> bytes{0xff, 0xff};
    CHECK(SerializeUserStorage(user, bytes));
    auto const original_bytes = bytes;
    CHECK(SerializeUserStorage(user, bytes));
    CHECK(bytes == original_bytes); // Replace, rather than append to the output.
    ae::RamDomainStorage decoded;
    decoded.SaveData(ae::DomainQuery{ae::ObjId{999}, 1, 0}, {42});
    CHECK(DeserializeUserStorage(decoded, bytes));
    CHECK(!decoded.state.contains(ae::ObjId{999}));
    CheckSnapshot(decoded, {1, 2, 3});
    CHECK(bytes == original_bytes);
    // Both codec directions work without creating a USER directory or file.
    CHECK(!std::filesystem::exists(path));
    CHECK(apptraverse::SaveUserStorageFile(user, user_profile));
    CHECK(ReadBytes(user_profile / "user.bin") == bytes);
    CHECK(std::filesystem::is_regular_file((user_profile / "user.bin")));
    CHECK(std::distance(std::filesystem::directory_iterator(path), {}) == 1);
  }
  {
    ae::RamDomainStorage user; auto const user_profile = path;
    CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
    CheckSnapshot(user, {1, 2, 3});
    ae::RamDomainStorage decoded;
    CHECK(DeserializeUserStorage(decoded, ReadBytes(user_profile / "user.bin")));
    CheckSnapshot(decoded, {1, 2, 3});
    user.SaveData(q, {5, 6});
    // Dirty RAM is invisible to a separate disk reader until explicit SaveFile.
    ae::RamDomainStorage old; auto const old_profile = path;
    CHECK(apptraverse::LoadUserStorageFile(old, old_profile));
    CHECK(old.state.at(object)->at(301).at(0) == ae::ObjectData({1,2,3}));
    CHECK(apptraverse::SaveUserStorageFile(user, user_profile)); // overwrite existing file

    // A byte snapshot can be persisted by a different host and read by FileLoad.
    auto const external_profile = path / "external";
    std::filesystem::create_directory(external_profile);
    std::vector<std::uint8_t> bytes;
    CHECK(SerializeUserStorage(user, bytes));
    {
      std::ofstream output{external_profile / "user.bin", std::ios::binary};
      output.write(reinterpret_cast<char const*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
      output.close();
      CHECK(output);
    }
    ae::RamDomainStorage external;
    CHECK(LoadUserStorageFile(external, external_profile));
    CheckSnapshot(external, {5, 6});

    // A late metadata failure must not replace previously decoded object data.
    external.SaveData(q, {77});
    external.object_scopes[object] = {object, ae::ObjId{98}, false};
    std::vector<std::uint8_t> before;
    CHECK(SerializeUserStorage(external, before));
    for (std::size_t size = 0; size < bytes.size(); ++size) {
      std::vector<std::uint8_t> truncated{bytes.begin(), bytes.begin() + size};
      CHECK(!DeserializeUserStorage(external, truncated));
      std::vector<std::uint8_t> after;
      CHECK(SerializeUserStorage(external, after));
      CHECK(after == before);
    }
    CHECK(LoadUserStorageFile(external, path / "missing"));
    CHECK(external.state.empty());
    CHECK(external.object_scopes.empty());
  }
  {
    ae::RamDomainStorage user; auto const user_profile = path;
    CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
    CHECK(user.state.at(object)->at(301).at(0) == ae::ObjectData({5,6}));
    user.SaveData(q, {99}); // destruction must not flush
  }
  ae::RamDomainStorage user; auto const user_profile = path;
  CHECK(apptraverse::LoadUserStorageFile(user, user_profile));
  CHECK(user.state.at(object)->at(301).at(0) == ae::ObjectData({5,6}));
  std::filesystem::remove_all(path);
  std::cout << "SINGLE_FILE_USER_STORAGE_OK\n";
}
