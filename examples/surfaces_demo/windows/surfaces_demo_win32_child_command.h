#ifndef APPTRAVERSE_EXAMPLE_SURFACES_DEMO_WIN32_CHILD_COMMAND_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_DEMO_WIN32_CHILD_COMMAND_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace apptraverse {
class SurfacePresenter;
}

namespace apptraverse::example::surfaces {

bool TryHandleDemoLibraryCloseChildCommand(SurfacePresenter* surface_presenter,
                                           WPARAM wparam, LPARAM lparam);

void EnsureSurfacesDemoWin32ChildCommandHook();

}  // namespace apptraverse::example::surfaces

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_DEMO_WIN32_CHILD_COMMAND_H_
