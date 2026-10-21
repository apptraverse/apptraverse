#include "surfaces_v1_package.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/surfaces/surfaces_demo_revision.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace apptraverse {

namespace {

std::optional<std::filesystem::path> ExecutableDirectory() {
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH]{};
  DWORD const length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return std::nullopt;
  }
  return std::filesystem::path(buffer).parent_path();
#else
  return std::nullopt;
#endif
}

std::filesystem::path LayerPath(std::filesystem::path const& root,
                                ae::ObjId object_id,
                                std::uint32_t class_id,
                                unsigned version) {
  return DirectoryDomainStorageLayerPath(
      root,
      ae::DomainQuery{object_id, class_id, static_cast<std::uint8_t>(version)});
}

void CopyFileIfMissing(std::filesystem::path const& source,
                       std::filesystem::path const& target) {
  if (std::filesystem::exists(target)) {
    return;
  }
  if (!std::filesystem::is_regular_file(source)) {
    return;
  }
  std::filesystem::create_directories(target.parent_path());
  std::filesystem::copy_file(source, target);
}

std::string PackagedSuffixForObject(ae::ObjId::Type object_id) {
  if (object_id == surfaces_demo::ToObjId(surfaces_demo::ObjId::Surface1)) {
    return " [demo-v1]";
  }
  return {};
}

std::string DefaultSuffixForObject(ae::ObjId::Type object_id,
                                   std::uint32_t surface_number) {
  if (auto packaged = PackagedSuffixForObject(object_id); !packaged.empty()) {
    return packaged;
  }
  return " [s" + std::to_string(surface_number) + "]";
}

bool ObjectHasSurfaceV0Layer(std::filesystem::path const& root,
                             ae::ObjId object_id) {
  return DirectoryDomainStorageLayerFileExists(
      root, ae::DomainQuery{object_id, Surface::kClassId, 0});
}

}  // namespace

std::filesystem::path SurfacesDemoBundledV1PackageDirectory() {
  if (char const* from_env = std::getenv("APPTRAVERSE_SURFACE_V1_PACKAGE");
      from_env != nullptr && from_env[0] != '\0') {
    return from_env;
  }
  if (auto exe_dir = ExecutableDirectory()) {
    return *exe_dir / "surface_v1_package";
  }
  return "surface_v1_package";
}

void SurfacesDemoInstallBundledV1Layers(
    std::filesystem::path const& state_dir,
    std::filesystem::path const& package_dir) {
  if (!std::filesystem::is_directory(package_dir)) {
    return;
  }
  if (!std::filesystem::is_directory(state_dir)) {
    return;
  }
  for (auto const& entry :
       std::filesystem::directory_iterator(
           state_dir, std::filesystem::directory_options::skip_permission_denied)) {
    if (!entry.is_directory()) {
      continue;
    }
    ae::ObjId::Type object_id = 0;
    try {
      object_id =
          static_cast<ae::ObjId::Type>(std::stoul(entry.path().filename().string()));
    } catch (...) {
      continue;
    }
    if (!ObjectHasSurfaceV0Layer(state_dir, ae::ObjId{object_id})) {
      continue;
    }
    auto const packaged = LayerPath(package_dir, ae::ObjId{object_id},
                                    Surface::kClassId, 1);
    auto const target =
        LayerPath(state_dir, ae::ObjId{object_id}, Surface::kClassId, 1);
    CopyFileIfMissing(packaged, target);
  }
}

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
void SurfacesDemoEnsureMissingSurfaceV1Layers(
    std::filesystem::path const& state_dir) {
  EnsureSurfacesRegistration();
  DirectoryDomainStorage storage{state_dir};
  if (!std::filesystem::is_directory(state_dir)) {
    return;
  }
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  for (auto const& entry :
       std::filesystem::directory_iterator(
           state_dir, std::filesystem::directory_options::skip_permission_denied)) {
    if (!entry.is_directory()) {
      continue;
    }
    ae::ObjId::Type raw_id = 0;
    try {
      raw_id =
          static_cast<ae::ObjId::Type>(std::stoul(entry.path().filename().string()));
    } catch (...) {
      continue;
    }
    ae::ObjId const object_id{raw_id};
    if (!ObjectHasSurfaceV0Layer(state_dir, object_id)) {
      continue;
    }
    if (DirectoryDomainStorageLayerFileExists(
            state_dir, ae::DomainQuery{object_id, Surface::kClassId, 1})) {
      continue;
    }
    ae::Ptr<ae::Obj> loaded;
    if (!graph.LoadRoot(object_id, loaded).IsOk() || !loaded ||
        loaded->GetClassId() != Surface::kClassId) {
      continue;
    }
    auto surface = ae::Ptr<Surface>{loaded};
    surface->window_title_suffix =
        DefaultSuffixForObject(raw_id, surface->number);
    storage.BeginSaveSession();
    if (!graph.SaveVersion(ae::Version<1>{}, *surface, object_id).IsOk() ||
        storage.SaveSessionHadFailure()) {
      throw std::runtime_error("EnsureMissingSurfaceV1Layers SaveVersion failed");
    }
  }
}
#else
void SurfacesDemoEnsureMissingSurfaceV1Layers(
    std::filesystem::path const& state_dir) {
  (void)state_dir;
}
#endif

}  // namespace apptraverse
