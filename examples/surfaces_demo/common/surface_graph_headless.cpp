#include "surfaces_presentation.h"

#include "surface_button_presenter.h"
#include "surface_language_button_presenter.h"
#include "surface_window_presenter.h"
#include "surfaces_chrome_wiring.h"
#include "surfaces_model.h"

namespace apptraverse {

void AttachPlatformSurfacePresenters(Surface& surface, Application& application,
                                     ae::Domain& domain) {
  auto window = SurfaceWindowPresenter::ptr::Create(ae::CreateWith{domain});
  auto add_button = SurfaceButtonPresenter::ptr::Create(ae::CreateWith{domain});
  auto close_button =
      SurfaceButtonPresenter::ptr::Create(ae::CreateWith{domain});
  auto language_button =
      SurfaceLanguageButtonPresenter::ptr::Create(ae::CreateWith{domain});

  auto surface_ref = Surface::ptr::MakeFromThis(&surface);
  window->surface = surface_ref;
  add_button->surface = surface_ref;
  close_button->surface = surface_ref;
  language_button->surface = surface_ref;

  add_button->window_presenter = window;
  close_button->window_presenter = window;
  language_button->window_presenter = window;

  add_button->role = SurfaceButtonRole::Add;
  add_button->control_id = 1001;
  close_button->role = SurfaceButtonRole::Close;
  close_button->control_id = 1002;
  language_button->control_id = 1003;

  surface.chrome.window = window;
  surface.chrome.add_button = add_button;
  surface.chrome.close_button = close_button;
  surface.chrome.language_button = language_button;

  WireSurfaceChromeLocalization(surface, application);
}

}  // namespace apptraverse
