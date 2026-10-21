#include "surfaces_evolution_ops.h"

#include "surfaces_distill.h"
#include "surfaces_v1_package.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/surfaces/surfaces_demo_revision.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_registration.h"

namespace apptraverse {

namespace {

using surfaces_demo::ObjId;
using surfaces_demo::ToObjId;

constexpr ae::ObjId kSurface1Id{ToObjId(ObjId::Surface1)};

bool DirectoryEmpty(std::filesystem::path const& path) {
  return !std::filesystem::exists(path) ||
         (std::filesystem::is_directory(path) &&
          std::filesystem::is_empty(path));
}

Surface& RequireSurface1(Application& application) {
  auto const& list = application.surfaces->surfaces;
  for (auto const& surface : list) {
    if (surface->obj_id == kSurface1Id) {
      return *surface;
    }
  }
  throw std::runtime_error("Surface1 missing from application graph");
}

bool SurfaceV1LayerMissing(std::filesystem::path const& state_dir,
                           ae::ObjId object_id) {
  return !DirectoryDomainStorageLayerFileExists(
      state_dir, ae::DomainQuery{object_id, Surface::kClassId, 1});
}

void PrintInspect(Application const& application) {
  std::cout << "{\"exe\":\""
            << (APPTRAVERSE_SURFACE_DEMO_REVISION == 0 ? 'A' : 'B')
            << "\",\"class_id\":" << Surface::kClassId << ",\"surfaces\":[";
  bool first = true;
  for (auto const& surface : application.surfaces->surfaces) {
    if (!first) {
      std::cout << ',';
    }
    first = false;
    std::cout << "{\"object_id\":" << surface->obj_id.id()
              << ",\"number\":" << surface->number
              << ",\"desktop_x\":" << surface->desktop_x
              << ",\"desktop_y\":" << surface->desktop_y
              << ",\"desktop_width\":" << surface->desktop_width
              << ",\"desktop_height\":" << surface->desktop_height;
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
    std::cout << ",\"window_title_suffix\":"
              << std::quoted(surface->window_title_suffix);
#endif
    std::cout << '}';
  }
  std::cout << "]}\n";
}

void SaveSurfaceV0(Surface& surface, DirectoryDomainStorage& storage,
                   ae::DomainGraph& graph) {
  storage.BeginSaveSession();
  if (!graph.SaveVersion(ae::Version<0>{}, surface, surface.obj_id).IsOk() ||
      storage.SaveSessionHadFailure()) {
    throw std::runtime_error("SaveVersion<0> failed");
  }
}

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
void SaveSurfaceV1(Surface& surface, DirectoryDomainStorage& storage,
                   ae::DomainGraph& graph) {
  storage.BeginSaveSession();
  if (!graph.SaveVersion(ae::Version<1>{}, surface, surface.obj_id).IsOk() ||
      storage.SaveSessionHadFailure()) {
    throw std::runtime_error("SaveVersion<1> failed");
  }
}
#endif

}  // namespace

int RunSurfacesEvolutionOp(std::filesystem::path const& state_dir,
                           std::string const& operation) {
  EnsureObjectRegistration();
  EnsureSurfacesRegistration();

  bool const new_state =
      operation == "create" || operation == "package" || operation == "bootstrap";
  if (new_state) {
    if (std::filesystem::exists(state_dir) && !DirectoryEmpty(state_dir)) {
      throw std::runtime_error("new state/package directory must be empty");
    }
  } else if (!std::filesystem::is_directory(state_dir)) {
    throw std::runtime_error("state directory does not exist");
  }

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 0
  if (operation == "package" || operation == "set-suffix" ||
      operation == "install-bundled") {
    throw std::runtime_error("v0 EXE does not support v1-only operations");
  }
#else
  if (operation == "create" || operation == "bootstrap") {
    throw std::runtime_error("create v0 user state with EXE A");
  }
  if (operation == "inspect" &&
      SurfaceV1LayerMissing(state_dir, kSurface1Id)) {
    std::cerr << "MISSING_STATE_LAYER class=" << Surface::kClassId
              << " object_id=" << kSurface1Id.id()
              << " version=1 (must be provided by packaged state)\n";
    return 2;
  }
#endif

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
  if (operation == "package") {
    DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    ae::DomainGraph graph{&domain};
    auto surface = Surface::ptr::Create(
        ae::CreateWith{domain}.with_id(kSurface1Id));
    surface->window_title_suffix = " [demo-v1]";
    SaveSurfaceV1(*surface, storage, graph);
    return 0;
  }

  if (operation == "install-bundled") {
    auto const package = SurfacesDemoBundledV1PackageDirectory();
    SurfacesDemoInstallBundledV1Layers(state_dir, package);
    SurfacesDemoEnsureMissingSurfaceV1Layers(state_dir);
    return 0;
  }
#endif

  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  ae::DomainGraph graph{&domain};
  Application::ptr application;

  if (operation == "create" || operation == "bootstrap") {
    application = BuildSurfacesGraph(domain);
    FinalizeDistilledGraph(*application);
    SaveDistilledRoot(*application);
    PrintInspect(*application);
    return 0;
  } else {
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
    if (operation != "inspect") {
      SurfacesDemoInstallBundledV1Layers(
          state_dir, SurfacesDemoBundledV1PackageDirectory());
      SurfacesDemoEnsureMissingSurfaceV1Layers(state_dir);
    }
#endif
    application = LoadApplication<Application>(
        domain, ae::ObjId{ToObjId(ObjId::Application)});
  }

  if (operation == "inspect") {
    PrintInspect(*application);
    return 0;
  }

  Surface& surface1 = RequireSurface1(*application);

  if (operation == "move") {
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 0
    surface1.desktop_x += 7;
    surface1.desktop_y += 9;
#else
    surface1.desktop_x += 13;
    surface1.desktop_y += 17;
#endif
    SaveSurfaceV0(surface1, storage, graph);
    PrintInspect(*application);
    return 0;
  }

#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
  if (operation == "set-suffix") {
    surface1.window_title_suffix += "+";
    SaveSurfaceV1(surface1, storage, graph);
    PrintInspect(*application);
    return 0;
  }
#endif

  if (operation == "add-surface") {
    surface1.AddSurface();
    SaveDistilledRoot(*application);
    PrintInspect(*application);
    return 0;
  }

  throw std::runtime_error("unsupported evolution operation");
}

}  // namespace apptraverse
