#pragma once
#include "wire.h"
namespace update_lab {
struct Installation {
  std::string phase, active, previous, candidate;
  unsigned active_version, candidate_version;
  AE_REFLECT_MEMBERS(phase, active, previous, candidate, active_version, candidate_version)
};
fs::path Cache(fs::path const& profile);
bool ReadInstalled(fs::path const&, InstalledImage&);
bool WriteInstalled(fs::path const&, InstalledImage const&);
bool LoadInstallation(fs::path const&, Installation&);
bool SaveInstallation(fs::path const&, Installation const&);
bool SnapshotProfile(fs::path const& installation, fs::path const& profile);
bool RestoreProfile(fs::path const& installation, fs::path const& profile);
bool StageExecutable(fs::path const& installation, fs::path const& profile, Bytes const&, unsigned version);
}
