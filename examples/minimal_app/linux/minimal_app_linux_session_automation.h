#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_LINUX_MINIMAL_APP_LINUX_SESSION_AUTOMATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_LINUX_MINIMAL_APP_LINUX_SESSION_AUTOMATION_H_

#include <functional>
#include <string_view>

#include "aether-objects/obj/obj.h"

namespace apptraverse {
class ApplicationModelRuntime;
}

namespace apptraverse::example::minimal {

enum class LinuxMinimalSessionAutomation {
  None,
  L3Save,
  L3ColdVerify,
  L3DetailClose,
};

LinuxMinimalSessionAutomation ParseLinuxMinimalSessionAutomation(
    std::string_view value);

struct LinuxMinimalSessionAutomationHooks {
  std::function<void()> request_stop;
  void OnAfterInitial(ae::Obj& ui_root);
  void OnAfterIncremental(ae::Obj& ui_root);
};

LinuxMinimalSessionAutomationHooks MakeLinuxMinimalSessionAutomationHooks(
    LinuxMinimalSessionAutomation mode,
    std::function<void()> request_stop,
    ApplicationModelRuntime* runtime = nullptr);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_LINUX_MINIMAL_APP_LINUX_SESSION_AUTOMATION_H_
