#include "win_presenters.h"

#include "apptraverse/materialized_ops.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER_MATERIALIZED(WinTextToolbarPresenter);
APPTRAVERSE_REGISTER_MATERIALIZED(WinColorToolbarPresenter);
APPTRAVERSE_REGISTER_MATERIALIZED(WinCenterStripPresenter);
APPTRAVERSE_REGISTER_MATERIALIZED(WinPaintWindowPresenter);
APPTRAVERSE_REGISTER_MATERIALIZED(WinLayoutWindowPresenter);
APPTRAVERSE_REGISTER_MATERIALIZED(WinPresentationApplication);

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
  auto strip = WinCenterStripPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(DemoObjId::WinCenterStripPresenter)));

  paint->window = application.window_a;
  layout->window = application.window_b;
  text->toolbar = application.window_b->text_toolbar;
  color->toolbar = application.window_b->color_toolbar;
  strip->strip = application.window_b->center_strip;
  layout->text_toolbar = text;
  layout->color_toolbar = color;
  layout->center_strip = strip;
  root->paint_window = paint;
  root->layout_window = layout;
  return root;
}

}  // namespace apptraverse
