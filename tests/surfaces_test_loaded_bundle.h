#ifndef APPTRAVERSE_TESTS_SURFACES_TEST_LOADED_BUNDLE_H_
#define APPTRAVERSE_TESTS_SURFACES_TEST_LOADED_BUNDLE_H_

#include <filesystem>
#include <memory>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse::test {

// Immovable owner: storage → Domain → Application::ptr (destruction reversed).
struct LoadedApplicationBundle {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  Application::ptr application;

  LoadedApplicationBundle();
  LoadedApplicationBundle(LoadedApplicationBundle const&) = delete;
  LoadedApplicationBundle& operator=(LoadedApplicationBundle const&) = delete;
  LoadedApplicationBundle(LoadedApplicationBundle&&) = delete;
  LoadedApplicationBundle& operator=(LoadedApplicationBundle&&) = delete;
  ~LoadedApplicationBundle();

  Application& app() { return *application; }
  Application const& app() const { return *application; }

  void VerifyOwnerInvariants() const;

  static std::unique_ptr<LoadedApplicationBundle> LoadFromModelCheckpoint(
      std::filesystem::path const& state_dir);
};

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_SURFACES_TEST_LOADED_BUNDLE_H_
