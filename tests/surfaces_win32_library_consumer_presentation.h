#pragma once

#include "apptraverse/surfaces/surface_platform_attach.h"

namespace apptraverse {

void AttachSurfacesWin32LibraryPlatformPresenters(Surface& surface,
                                                  Application& application,
                                                  ae::Domain& domain);

}
