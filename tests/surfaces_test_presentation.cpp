#include "surfaces_presentation.h"

#include "apptraverse/object_macros.h"

#include "surface_button_presenter.h"
#include "surface_language_button_presenter.h"
#include "surface_window_presenter.h"
#include "surfaces_chrome_wiring.h"
#include "surfaces_model.h"
#include "surfaces_test_presenter_counters.h"

namespace apptraverse {
namespace test {

class TestSurfaceWindowPresenter : public SurfaceWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::TestSurfaceWindowPresenter",
      TestSurfaceWindowPresenter, SurfaceWindowPresenter, 0)

 protected:
  TestSurfaceWindowPresenter() = default;

 public:
  explicit TestSurfaceWindowPresenter(ae::ObjProp prop)
      : SurfaceWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override {
    ++TestSurfacePresenter::on_load_calls;
    TestSurfacePresenter::loaded_ids.push_back(obj_id.id());
  }
  void OnModelChanged() override { ++TestSurfacePresenter::on_model_changed_calls; }
  void OnUnload() override {
    ++TestSurfacePresenter::on_unload_calls;
    TestSurfacePresenter::unloaded_ids.push_back(obj_id.id());
  }
};

class TestSurfaceButtonPresenter : public SurfaceButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::TestSurfaceButtonPresenter",
      TestSurfaceButtonPresenter, SurfaceButtonPresenter, 0)

 protected:
  TestSurfaceButtonPresenter() = default;

 public:
  explicit TestSurfaceButtonPresenter(ae::ObjProp prop)
      : SurfaceButtonPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override {
    ++TestSurfacePresenter::on_load_calls;
    TestSurfacePresenter::loaded_ids.push_back(obj_id.id());
  }
  void OnModelChanged() override { ++TestSurfacePresenter::on_model_changed_calls; }
  void OnUnload() override {
    ++TestSurfacePresenter::on_unload_calls;
    TestSurfacePresenter::unloaded_ids.push_back(obj_id.id());
  }
};

class TestSurfaceLanguageButtonPresenter
    : public SurfaceLanguageButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::TestSurfaceLanguageButtonPresenter",
      TestSurfaceLanguageButtonPresenter, SurfaceLanguageButtonPresenter, 0)

 protected:
  TestSurfaceLanguageButtonPresenter() = default;

 public:
  explicit TestSurfaceLanguageButtonPresenter(ae::ObjProp prop)
      : SurfaceLanguageButtonPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override {
    ++TestSurfacePresenter::on_load_calls;
    TestSurfacePresenter::loaded_ids.push_back(obj_id.id());
  }
  void OnModelChanged() override { ++TestSurfacePresenter::on_model_changed_calls; }
  void OnUnload() override {
    ++TestSurfacePresenter::on_unload_calls;
    TestSurfacePresenter::unloaded_ids.push_back(obj_id.id());
  }
};

APPTRAVERSE_REGISTER(TestSurfaceWindowPresenter);
APPTRAVERSE_REGISTER(TestSurfaceButtonPresenter);
APPTRAVERSE_REGISTER(TestSurfaceLanguageButtonPresenter);

}  // namespace test

void AttachPlatformSurfacePresenters(Surface& surface, Application& application,
                                     ae::Domain& domain) {
  auto window = test::TestSurfaceWindowPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto add_button = test::TestSurfaceButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto close_button = test::TestSurfaceButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  auto language_button = test::TestSurfaceLanguageButtonPresenter::ptr::Create(
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
