#include "minimal_app_android_graph.h"

#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/runtime_node.h"

#include "minimal_app_graph_builder.h"

namespace apptraverse::example::minimal {

MinimalApplication::ptr BuildAndroidMinimalApplicationGraph(ae::Domain& domain) {
  ui::android::EnsureAndroidUiPresenterRegistration();
  MinimalPresenterFactories factories;
  factories.create_window = [](ae::Domain& d, ae::ObjId presenter_id) {
    return ui::android::AndroidDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{d}.with_id(presenter_id));
  };
  factories.create_button = [](ae::Domain& d, ae::ObjId presenter_id) {
    return ui::android::AndroidPushButtonPresenter::ptr::Create(
        ae::CreateWith{d}.with_id(presenter_id));
  };
  factories.create_list = [](ae::Domain& d, ae::ObjId presenter_id) {
    return ui::android::AndroidListContainerPresenter::ptr::Create(
        ae::CreateWith{d}.with_id(presenter_id));
  };
  return BuildMinimalApplicationGraph(domain, factories);
}

}  // namespace apptraverse::example::minimal
