#include "win_presenters.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(WinTextToolbarPresenter);
APPTRAVERSE_REGISTER(WinColorToolbarPresenter);
APPTRAVERSE_REGISTER(WinPaintWindowPresenter);
APPTRAVERSE_REGISTER(WinLayoutWindowPresenter);
APPTRAVERSE_REGISTER(WinPresentationApplication);

}  // namespace

void EnsureWindowsPresenterRegistration() { EnsureObjectRegistration(); }

WinPresentationApplication::ptr BuildPresentationGraph(
    ae::Domain& domain, Application& application) {
  using demo::DemoObjId;
  using demo::ToObjId;

  auto root = WinPresentationApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(DemoObjId::WinPresentationApplication)));
  auto paint = WinPaintWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(DemoObjId::WinPaintWindowPresenter)));
  auto layout = WinLayoutWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(DemoObjId::WinLayoutWindowPresenter)));
  auto text = WinTextToolbarPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(DemoObjId::WinTextToolbarPresenter)));
  auto color = WinColorToolbarPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(DemoObjId::WinColorToolbarPresenter)));

  paint->window = application.window_a;
  layout->window = application.window_b;
  text->toolbar = application.window_b->text_toolbar;
  color->toolbar = application.window_b->color_toolbar;
  layout->text_toolbar = text;
  layout->color_toolbar = color;
  root->paint_window = paint;
  root->layout_window = layout;
  return root;
}

}  // namespace apptraverse
