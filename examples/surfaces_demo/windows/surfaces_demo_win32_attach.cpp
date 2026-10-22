#include "surfaces_demo_win32_attach.h"

#include "apptraverse/platform/windows/surfaces_win_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/surfaces/surfaces_chrome_wiring.h"
#include "apptraverse/surfaces/surfaces_ids.h"
#include "apptraverse/surfaces/surfaces_presentation.h"

#include "surfaces_demo_add_button.h"
#include "surfaces_demo_close_button.h"
#include "surfaces_demo_language_button.h"
#include "surfaces_demo_surface_window_host.h"

namespace apptraverse::example::surfaces {

void AttachSurfacesDemoWin32SurfacePresenters(Surface& surface,
                                              Application& application,
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
  auto chrome_host =
      SurfacesDemoSurfaceWindowHostPresenter::ptr::Create(ae::CreateWith{domain});
  auto add_button = SurfacesDemoAddButton::ptr::Create(ae::CreateWith{domain});
  auto add_presenter =
      ui::windows::WinPushButtonPresenter::ptr::Create(ae::CreateWith{domain});
  auto language_button =
      SurfacesDemoLanguageButton::ptr::Create(ae::CreateWith{domain});
  auto language_presenter =
      ui::windows::WinPushButtonPresenter::ptr::Create(ae::CreateWith{domain});
  auto close_button =
      SurfacesDemoCloseButton::ptr::Create(ae::CreateWith{domain});
  auto close_presenter =
      ui::windows::WinPushButtonPresenter::ptr::Create(ae::CreateWith{domain});

  auto surface_ref = Surface::ptr::MakeFromThis(&surface);
  window->surface = surface_ref;
  add_button->surface = surface_ref;
  add_button->label = application.label_add;
  language_button->locale = application.locale;
  language_button->label = application.language_switch_label;
  close_button->surface = surface_ref;
  close_button->label = application.label_close;

  chrome_host->surface_window = window;
  chrome_host->locale = application.locale;

  add_presenter->button = add_button;
  add_presenter->window_presenter = chrome_host;
  add_presenter->layout_x = kSurfaceAddButtonLayoutX;
  add_presenter->layout_y = kSurfaceAddButtonLayoutY;
  add_presenter->layout_width = kSurfaceAddButtonLayoutWidth;
  add_presenter->layout_height = kSurfaceAddButtonLayoutHeight;
  add_presenter->control_id = static_cast<std::uint32_t>(kSurfaceAddButtonId);

  language_presenter->button = language_button;
  language_presenter->window_presenter = chrome_host;
  language_presenter->control_id =
      static_cast<std::uint32_t>(kSurfaceLanguageButtonId);

  close_presenter->button = close_button;
  close_presenter->window_presenter = chrome_host;
  close_presenter->control_id =
      static_cast<std::uint32_t>(kSurfaceCloseButtonId);
  close_presenter->layout_x = kSurfaceAddButtonLayoutX;
  close_presenter->layout_y = kSurfaceAddButtonLayoutY;
  close_presenter->layout_width = kSurfaceAddButtonLayoutWidth;
  close_presenter->layout_height = kSurfaceAddButtonLayoutHeight;

  surface.chrome.window = window;
  surface.chrome.add_button = add_presenter;
  surface.chrome.close_button = close_presenter;
  surface.chrome.language_button = language_presenter;

  WireSurfaceChromeLocalization(surface, application);
  if (auto demo_lang = SurfacesDemoLanguageButton::ptr{language_button}) {
    demo_lang->locale = application.locale;
  }
}

}  // namespace apptraverse::example::surfaces
