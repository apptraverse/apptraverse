#include "surfaces_runtime_execution.h"

#include <cstdio>

#include "aether-objects/obj/registry.h"

#include "apptraverse/node.h"
#include "apptraverse/noninteractive_crt.h"

#include "surfaces_demo_platform_attach.h"

namespace apptraverse {
namespace {

[[noreturn]] void FatalSurfacesGraph(char const* detail) {
  char message[768];
  std::snprintf(message, sizeof(message),
                "fatal: Surfaces persistent graph: %s\n", detail);
  WriteFatalStderr(message);
  std::abort();
}

Surfaces const* LoadedCanonicalSurfaces(Application const& application) {
  if (!application.surfaces.is_valid()) {
    FatalSurfacesGraph("application.surfaces invalid");
  }
  if (!application.surfaces.is_loaded()) {
    FatalSurfacesGraph("application.surfaces not loaded");
  }
  return &*application.surfaces;
}

void CheckDomainObject(ae::Domain const& domain, ae::Obj const& object,
                       char const* label) {
  ae::Ptr<ae::Obj> const found = domain.Find(object.obj_id);
  if (!found) {
    FatalSurfacesGraph(label);
  }
  if (found.get() != &object) {
    char message[256];
    std::snprintf(message, sizeof(message),
                  "%s domain.Find id=%u returned %p expected %p", label,
                  object.obj_id.id(), static_cast<void*>(found.get()),
                  static_cast<void const*>(&object));
    FatalSurfacesGraph(message);
  }
}

void CheckSurfaceParent(Surfaces const& canonical, Surface const& surface) {
  if (!surface.surfaces.is_valid()) {
    char message[128];
    std::snprintf(message, sizeof(message),
                  "Surface id=%u missing surfaces back-link",
                  surface.obj_id.id());
    FatalSurfacesGraph(message);
  }
  if (!surface.surfaces.is_loaded()) {
    char message[128];
    std::snprintf(message, sizeof(message),
                  "Surface id=%u surfaces back-link not loaded",
                  surface.obj_id.id());
    FatalSurfacesGraph(message);
  }
  if (surface.surfaces.id() != canonical.obj_id) {
    char message[256];
    std::snprintf(message, sizeof(message),
                  "Surface id=%u surfaces->id=%u expected Surfaces id=%u",
                  surface.obj_id.id(), surface.surfaces.id().id(),
                  canonical.obj_id.id());
    FatalSurfacesGraph(message);
  }
  if (&*surface.surfaces != &canonical) {
    char message[256];
    std::snprintf(message, sizeof(message),
                  "Surface id=%u surfaces->ptr=%p expected canonical Surfaces "
                  "at %p",
                  surface.obj_id.id(),
                  static_cast<void const*>(&*surface.surfaces),
                  static_cast<void const*>(&canonical));
    FatalSurfacesGraph(message);
  }
}

}  // namespace

void VerifySurfacesPersistentGraph(Application const& application) {
  CheckDomainObject(*application.domain, application, "Application");

  Surfaces const* const canonical = LoadedCanonicalSurfaces(application);
  CheckDomainObject(*application.domain, *canonical, "Surfaces");

  if (ae::Registry::GetRegistry().GenerationDistance(
          Node::kClassId, canonical->GetClassId()) < 0) {
    FatalSurfacesGraph("canonical container is not a Node");
  }

  for (Surface::ptr const& live : canonical->surfaces) {
    if (!live.is_valid() || !live.is_loaded()) {
      FatalSurfacesGraph("live Surface entry not loaded");
    }
    CheckDomainObject(*application.domain, *live, "Surface");
    CheckSurfaceParent(*canonical, *live);

    if (live->chrome.window.is_valid() && live->chrome.window.is_loaded()) {
      if (live->chrome.window->surface.is_valid() &&
          live->chrome.window->surface.is_loaded()) {
        if (live->chrome.window->surface.id() != live->obj_id) {
          FatalSurfacesGraph("presenter surface ref id mismatch");
        }
        if (&*live->chrome.window->surface != &*live) {
          FatalSurfacesGraph("presenter surface ref object mismatch");
        }
      }
    }
  }

  if (application.locale.is_valid() && application.locale.is_loaded()) {
    CheckDomainObject(*application.domain, *application.locale,
                      "UiLocaleSettings");
  }
}

void WireSurfacesDemoRuntimeExecution(Application& application) {
  VerifySurfacesPersistentGraph(application);
  Surfaces& surfaces = *application.surfaces;
  surfaces.host_application = &application;
  WireSurfacesDemoPlatformBindings(surfaces);
}

}  // namespace apptraverse
