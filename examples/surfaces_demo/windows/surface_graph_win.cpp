#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/surfaces/surfaces_chrome_wiring.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_presentation.h"

#include "surfaces_demo_add_button.h"
#include "surfaces_demo_surface_window_host.h"

namespace apptraverse {

void AttachPlatformSurfacePresenters(Surface& surface, Application& application,
                                     ae::Domain& domain) {
  using surfaces_demo::ObjId;
  using surfaces_demo::ToObjId;

  Win32SurfaceWindowPresenter::ptr window;
  if (surface.obj_id.id() == ToObjId(ObjId::Surface1)) {
    window = Win32SurfaceWindowPresenter::ptr::Create(
        ae::CreateWith{domain}.with_id(ToObjId(ObjId::Surface1Presenter)));
  } else {
    window = Win32SurfaceWindowPresenter::ptr::Create(ae::CreateWith{domain});
  }
  auto add_host = example::surfaces::SurfacesDemoSurfaceWindowHostPresenter::
      ptr::Create(ae::CreateWith{domain});
  auto add_button =
      example::surfaces::SurfacesDemoAddButton::ptr::Create(
          ae::CreateWith{domain});
  auto add_presenter = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto close_button = Win32SurfaceButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto language_button = Win32SurfaceLanguageButtonPresenter::ptr::Create(
      ae::CreateWith{domain});

  auto surface_ref = Surface::ptr::MakeFromThis(&surface);
  window->surface = surface_ref;
  add_button->surface = surface_ref;
  add_button->label = application.label_add;
  close_button->surface = surface_ref;
  language_button->surface = surface_ref;

  add_host->surface_window = window;
  add_host->locale = application.locale;

  add_presenter->button = add_button;
  add_presenter->window_presenter = add_host;
  add_presenter->layout_x = kSurfaceAddButtonLayoutX;
  add_presenter->layout_y = kSurfaceAddButtonLayoutY;
  add_presenter->layout_width = kSurfaceAddButtonLayoutWidth;
  add_presenter->layout_height = kSurfaceAddButtonLayoutHeight;
  add_presenter->control_id =
      static_cast<std::uint32_t>(kSurfaceAddButtonId);

  close_button->window_presenter = window;
  language_button->window_presenter = window;

  close_button->role = SurfaceButtonRole::Close;
  close_button->control_id =
      static_cast<std::uint32_t>(kSurfaceCloseButtonId);
  language_button->control_id =
      static_cast<std::uint32_t>(kSurfaceLanguageButtonId);

  surface.chrome.window = window;
  surface.chrome.add_button = add_presenter;
  surface.chrome.close_button = close_button;
  surface.chrome.language_button = language_button;

  WireSurfaceChromeLocalization(surface, application);
}

}  // namespace apptraverse
