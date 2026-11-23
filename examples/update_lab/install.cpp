#include "install.h"
namespace update_lab {
bool BindApplication(fs::path const& directory, std::string const& app) {
  auto marker = directory / "application-id";
  Bytes expected{app.begin(), app.end()};
  if (fs::exists(marker)) {
    Bytes existing;
    return ReadFile(marker, existing, 48) && existing == expected;
  }
  if (app.empty()) return true;
  if (fs::exists(directory / "user.bin") || fs::exists(directory / "local/installed.bin") ||
      fs::exists(directory / "installation.bin")) return false;
  return AtomicWrite(marker, expected);
}
fs::path Cache(fs::path const& profile) { return profile / "local" / "packages-lzma1"; }
namespace {
struct LocalSnapshot {
  unsigned revision;
  Bytes native;
  AE_REFLECT_MEMBERS(revision, native)
};
}
bool HasCachedComponent(fs::path const& cache, std::string const& id) {
  return fs::is_regular_file(cache / FileId(id)) || fs::is_regular_file(cache / (id + ".native"));
}
bool LoadCachedState(fs::path const& cache, std::string const& id, ae::RamDomainStorage& storage, unsigned& revision) {
  Bytes bytes;
  auto path = cache / (id + ".native");
  if (!fs::is_regular_file(path))
    return ReadFile(cache / FileId(id), bytes) && ApplyStatePackage(bytes, storage, revision);
  LocalSnapshot snapshot;
  if (!ReadFile(path, bytes) || !Decode(std::move(bytes), snapshot) ||
      !apptraverse::DeserializeNativeStorage(storage, snapshot.native)) return false;
  revision = snapshot.revision;
  return true;
}
bool SaveCachedState(fs::path const& cache, std::string const& id, ae::RamDomainStorage const& storage, unsigned revision) {
  LocalSnapshot snapshot{revision, {}};
  if (!apptraverse::SerializeNativeStorage(storage, snapshot.native)) return false;
  auto bytes = Encode(snapshot);
  return !bytes.empty() && AtomicWrite(cache / (id + ".native"), bytes);
}
bool ReadInstalled(fs::path const& profile, InstalledImage& installed) {
  Bytes bytes;
  return ReadFile(profile / "local" / "installed.bin", bytes) && Decode(bytes, installed);
}
bool WriteInstalled(fs::path const& profile, InstalledImage const& installed) {
  return AtomicWrite(profile / "local" / "installed.bin", Encode(installed));
}
bool LoadInstallation(fs::path const& p, Installation& i) {
  Bytes b; return ReadFile(p / "installation.bin", b) && Decode(b, i);
}
bool SaveInstallation(fs::path const& p, Installation const& i) {
  return AtomicWrite(p / "installation.bin", Encode(i));
}
bool SnapshotProfile(fs::path const& install, fs::path const& profile) {
  for (auto const& relative : {fs::path{"user.bin"}, fs::path{"local/user.bin"}, fs::path{"local/installed.bin"}}) {
    if (!CopyAtomic(profile / relative, install / "rollback" / relative)) return false;
  }
  return true;
}
bool RestoreProfile(fs::path const& install, fs::path const& profile) {
  for (auto const& relative : {fs::path{"user.bin"}, fs::path{"local/user.bin"}, fs::path{"local/installed.bin"}})
    if (!CopyAtomic(install / "rollback" / relative, profile / relative)) return false;
  return AtomicWrite(profile / "local" / "rolled-back", Bytes{'1'});
}
bool StageExecutable(fs::path const& install, fs::path const& profile, Bytes const& bytes, unsigned version) {
  Installation i; StatePackage package; Bytes executable;
  if (!LoadInstallation(install, i) || !UnpackRaw(bytes, package, executable) ||
      package.kind != 2 || package.from != version || package.to != version) return false;
  auto slot = "E" + std::to_string(version);
  auto path = install / "slots" / slot / BinaryName();
  if (!AtomicWrite(path, executable)) return false;
#ifndef _WIN32
  std::error_code ec;
  fs::permissions(path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
      fs::perm_options::add, ec);
  if (ec) return false;
#endif
  if (!SnapshotProfile(install, profile)) return false;
  i.previous = i.active; i.candidate = slot; i.candidate_version = version; i.phase = "staged";
  return SaveInstallation(install, i);
}
}
