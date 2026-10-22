#include "apptraverse/platform/windows/surfaces_win_presenters.h"

#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"

#include "apptraverse/surfaces/surfaces_chrome_wiring.h"

#include "apptraverse/surfaces/surfaces_ids.h"

#include "apptraverse/surfaces/surfaces_presentation.h"



#include "surfaces_demo_add_button.h"

#include "surfaces_demo_language_button.h"

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

  auto chrome_host = example::surfaces::SurfacesDemoSurfaceWindowHostPresenter::

      ptr::Create(ae::CreateWith{domain});

  auto add_button =

      example::surfaces::SurfacesDemoAddButton::ptr::Create(

          ae::CreateWith{domain});

  auto add_presenter = ui::windows::WinPushButtonPresenter::ptr::Create(

      ae::CreateWith{domain});

  auto language_button =

      example::surfaces::SurfacesDemoLanguageButton::ptr::Create(

          ae::CreateWith{domain});

  auto language_presenter = ui::windows::WinPushButtonPresenter::ptr::Create(

      ae::CreateWith{domain});

  auto close_button = Win32SurfaceButtonPresenter::ptr::Create(

      ae::CreateWith{domain});



  auto surface_ref = Surface::ptr::MakeFromThis(&surface);

  window->surface = surface_ref;

  add_button->surface = surface_ref;

  add_button->label = application.label_add;

  language_button->locale = application.locale;

  language_button->label = application.language_switch_label;

  close_button->surface = surface_ref;



  chrome_host->surface_window = window;

  chrome_host->locale = application.locale;



  add_presenter->button = add_button;

  add_presenter->window_presenter = chrome_host;

  add_presenter->layout_x = kSurfaceAddButtonLayoutX;

  add_presenter->layout_y = kSurfaceAddButtonLayoutY;

  add_presenter->layout_width = kSurfaceAddButtonLayoutWidth;

  add_presenter->layout_height = kSurfaceAddButtonLayoutHeight;

  add_presenter->control_id =

      static_cast<std::uint32_t>(kSurfaceAddButtonId);



  language_presenter->button = language_button;

  language_presenter->window_presenter = chrome_host;

  language_presenter->control_id =

      static_cast<std::uint32_t>(kSurfaceLanguageButtonId);

  language_presenter->layout_x = kSurfaceAddButtonLayoutX;

  language_presenter->layout_y = kSurfaceAddButtonLayoutY;

  language_presenter->layout_width = kSurfaceAddButtonLayoutWidth;

  language_presenter->layout_height = kSurfaceAddButtonLayoutHeight;



  close_button->window_presenter = window;

  close_button->role = SurfaceButtonRole::Close;

  close_button->control_id =

      static_cast<std::uint32_t>(kSurfaceCloseButtonId);



  surface.chrome.window = window;

  surface.chrome.add_button = add_presenter;

  surface.chrome.close_button = close_button;

  surface.chrome.language_button = language_presenter;



  WireSurfaceChromeLocalization(surface, application);

}



}  // namespace apptraverse

