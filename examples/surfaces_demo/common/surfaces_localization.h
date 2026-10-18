#ifndef APPTRAVERSE_SURFACES_LOCALIZATION_H_
#define APPTRAVERSE_SURFACES_LOCALIZATION_H_

#include <cstdint>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_locale_settings.h"

#include "surfaces_model.h"

namespace apptraverse {

// Creates shared UiLocaleSettings + UiLocalizationString nodes on the persisted graph.
void InstallSurfacesDemoLocalization(Application& application,
                                     ae::Domain& domain);

std::string FormatSurfaceWindowTitle(ui::UiLocalizationString const& prefix,
                                     int language_index, std::uint32_t number);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_LOCALIZATION_H_
