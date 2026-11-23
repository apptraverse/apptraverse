#include "install.h"
#include "state_package.h"
namespace update_lab {
fs::path Cache(fs::path const& profile) { return profile / "local" / "packages"; }
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
  if (!LoadInstallation(install, i) || !UnpackRaw(bytes, package, executable)) return false;
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
