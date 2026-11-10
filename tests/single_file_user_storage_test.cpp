#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/overlay_domain_storage.h"

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK " #x << " line " << __LINE__ << '\n'; std::exit(1); } } while(false)

int main() {
  using apptraverse::SingleFileUserStorage;
  auto path = std::filesystem::temp_directory_path() /
      ("single-user-" + std::to_string(ae::ObjId::GenerateUnique().id()));
  ae::ObjId const object{101}, removed{102};
  ae::DomainQuery const q{object, 301, 0};
  ae::ObjectScope const scope{object, ae::ObjId{99}, true};
  {
    SingleFileUserStorage user{path};
    CHECK(user.LoadFile());
    user.SaveData(q, {1, 2, 3});
    user.SaveData(ae::DomainQuery{object, 301, 7}, {8, 9});
    user.Remove(removed);
    user.object_scopes[object] = scope;
    user.object_scopes[ae::ObjId{103}] = {}; // explicit reset, not missing
    CHECK(!std::filesystem::exists(path));
    CHECK(user.SaveFile());
    CHECK(std::filesystem::is_regular_file(user.file()));
    CHECK(std::distance(std::filesystem::directory_iterator(path), {}) == 1);
  }
  {
    SingleFileUserStorage user{path};
    CHECK(user.LoadFile());
    CHECK(user.state.at(object)->at(301).at(0) == ae::ObjectData({1,2,3}));
    CHECK(user.state.at(object)->at(301).at(7) == ae::ObjectData({8,9}));
    CHECK(user.ReadObjectScope(object) == scope);
    CHECK(user.IsConstantObject(object));
    CHECK(user.ReadObjectScope(ae::ObjId{103}).has_value());
    CHECK(!user.ReadObjectScope(ae::ObjId{104}).has_value());
    ae::RamDomainStorage backing;
    backing.SaveData(ae::DomainQuery{removed, 1, 0}, {4});
    apptraverse::OverlayDomainStorage overlay{user, backing};
    CHECK(overlay.Load(ae::DomainQuery{removed, 1, 0}).result == ae::DomainLoadResult::kRemoved);
    user.SaveData(q, {5, 6});
    // Dirty RAM is invisible to a separate disk reader until explicit SaveFile.
    SingleFileUserStorage old{path};
    CHECK(old.LoadFile());
    CHECK(old.state.at(object)->at(301).at(0) == ae::ObjectData({1,2,3}));
    CHECK(user.SaveFile()); // overwrite existing file
  }
  {
    SingleFileUserStorage user{path};
    CHECK(user.LoadFile());
    CHECK(user.state.at(object)->at(301).at(0) == ae::ObjectData({5,6}));
    user.SaveData(q, {99}); // destruction must not flush
  }
  SingleFileUserStorage user{path};
  CHECK(user.LoadFile());
  CHECK(user.state.at(object)->at(301).at(0) == ae::ObjectData({5,6}));
  { std::ofstream corrupt{user.file(), std::ios::trunc}; corrupt << "invalid"; }
  CHECK(!user.LoadFile());
  CHECK(user.state.at(object)->at(301).at(0) == ae::ObjectData({5,6}));
  auto blocked = path / "not-a-directory";
  { std::ofstream out{blocked}; out << 'x'; }
  SingleFileUserStorage cannot_save{blocked};
  CHECK(!cannot_save.SaveFile());
  std::filesystem::remove_all(path);
  std::cout << "SINGLE_FILE_USER_STORAGE_OK\n";
}
