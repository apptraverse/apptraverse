#ifndef APPTRAVERSE_EXAMPLE_SURFACES_LINUX_WINDOW_CATALOG_LINUX_SESSION_AUTOMATION_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_LINUX_WINDOW_CATALOG_LINUX_SESSION_AUTOMATION_H_

#include <functional>
#include <string_view>

#include "aether-objects/obj/obj.h"

namespace apptraverse {
class ApplicationModelRuntime;
}

namespace apptraverse::example::surfaces {

enum class LinuxWindowCatalogSessionAutomation {
  None,
  L3Save,
  L3ColdVerify,
  L3Cycle2Save,
  L3Cycle2ColdVerify,
  L3GeomZSave,
  L3GeomZCold,
  L3GeomZCycle2Save,
  L3GeomZCycle2Cold,
  L3ChildCloseSave,
  L3ChildCloseCold,
  NativeGeometry,
};

LinuxWindowCatalogSessionAutomation ParseLinuxWindowCatalogSessionAutomation(
    std::string_view value);

struct LinuxWindowCatalogSessionAutomationHooks {
  std::function<void()> request_stop;
  void OnAfterInitial(ae::Obj& ui_root);
  void OnAfterIncremental(ae::Obj& ui_root);
};

LinuxWindowCatalogSessionAutomationHooks
MakeLinuxWindowCatalogSessionAutomationHooks(
    LinuxWindowCatalogSessionAutomation mode,
    std::function<void()> request_stop,
    ApplicationModelRuntime* runtime = nullptr);

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_LINUX_WINDOW_CATALOG_LINUX_SESSION_AUTOMATION_H_
