#include "surfaces_presentation.h"

#include "surfaces_chrome_wiring.h"
#include "surfaces_ids.h"
#include "win_presenters.h"

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
  auto add_button = Win32SurfaceButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto close_button = Win32SurfaceButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto language_button = Win32SurfaceLanguageButtonPresenter::ptr::Create(
      ae::CreateWith{domain});

  auto surface_ref = Surface::ptr::MakeFromThis(&surface);
  window->surface = surface_ref;
  add_button->surface = surface_ref;
  close_button->surface = surface_ref;
  language_button->surface = surface_ref;

  add_button->window_presenter = window;
  close_button->window_presenter = window;
  language_button->window_presenter = window;

  add_button->role = SurfaceButtonRole::Add;
  add_button->control_id = static_cast<std::uint32_t>(kSurfaceAddButtonId);
  close_button->role = SurfaceButtonRole::Close;
  close_button->control_id =
      static_cast<std::uint32_t>(kSurfaceCloseButtonId);
  language_button->control_id =
      static_cast<std::uint32_t>(kSurfaceLanguageButtonId);

  surface.chrome.window = window;
  surface.chrome.add_button = add_button;
  surface.chrome.close_button = close_button;
  surface.chrome.language_button = language_button;

  WireSurfaceChromeLocalization(surface, application);
}

}  // namespace apptraverse
