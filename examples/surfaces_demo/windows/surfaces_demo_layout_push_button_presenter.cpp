#include "surfaces_demo_layout_push_button_presenter.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "surfaces_demo_surface_window_host.h"

namespace apptraverse::example::surfaces {

void SurfacesDemoLayoutPushButtonPresenter::SyncLayoutFieldsFromSurfaceChrome() {
  auto host = SurfacesDemoSurfaceWindowHostPresenter::ptr{window_presenter};
  if (!host || !host->surface_window) {
    return;
  }
  ComputeSurfaceChromeButtonLayout(*host->surface_window, control_id, &layout_x,
                                   &layout_y, &layout_width, &layout_height);
}

void SurfacesDemoLayoutPushButtonPresenter::OnLoad() {
  SyncLayoutFieldsFromSurfaceChrome();
  WinPushButtonPresenter::OnLoad();
}

void SurfacesDemoLayoutPushButtonPresenter::OnModelChanged() {
  SyncLayoutFieldsFromSurfaceChrome();
  WinPushButtonPresenter::OnModelChanged();
}

}  // namespace apptraverse::example::surfaces
