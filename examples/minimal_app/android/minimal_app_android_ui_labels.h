#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_UI_LABELS_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_UI_LABELS_H_

#include <cstdint>

namespace ae {
class Domain;
}

namespace apptraverse::example::minimal {

char const* ResolveMinimalAndroidPushButtonLabelUtf8(std::uint32_t control_id,
                                                      ae::Domain& domain);

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_ANDROID_UI_LABELS_H_
