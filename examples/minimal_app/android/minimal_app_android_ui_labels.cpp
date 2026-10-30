#include "minimal_app_android_ui_labels.h"

#include "minimal_app_controls.h"

namespace apptraverse::example::minimal {

char const* ResolveMinimalAndroidPushButtonLabelUtf8(std::uint32_t control_id,
                                                      ae::Domain& domain) {
  (void)domain;
  if (control_id == kMinimalAuxiliaryOpenerControlId) {
    return "Open Detail";
  }
  return nullptr;
}

}  // namespace apptraverse::example::minimal
