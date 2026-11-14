#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_

#include "minimal_app_controls.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse {
}  // namespace apptraverse

namespace apptraverse::example::minimal {

struct MinimalMainPresenters {
  ui::UiPushButtonPresenter::ptr apply_button;
  ui::UiListContainerPresenter::ptr row_list;
  ui::UiPushButtonPresenter::ptr auxiliary_opener;

  AE_REFLECT_MEMBERS(apply_button, row_list, auxiliary_opener)
};

struct MinimalAuxiliaryPresenters {
  ui::UiPushButtonPresenter::ptr apply_button;
  ui::UiListContainerPresenter::ptr row_list;
  ui::UiPushButtonPresenter::ptr closer;

  AE_REFLECT_MEMBERS(apply_button, row_list, closer)
};

class MinimalApplication;

class MinimalWindow : public NodeFor<MinimalWindow, ui::UiDesktopWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalWindow",
      MinimalWindow, ui::UiDesktopWindow, 0)
 protected:
  MinimalWindow() = default;
  void OnClientSizeChanged() override;
 public:
  explicit MinimalWindow(ae::ObjProp prop) : NodeFor{prop} {}
  ae::ObjPtr<MinimalApplication> application;
  AE_OBJECT_REFLECT(AE_MMBR(application))
  template <typename D> void Load(ae::Version<0>, D& d) { d(base_, application); }
  template <typename D> void Save(ae::Version<0>, D& d) const { d(base_, application); }
};

class MinimalAuxiliaryOpenerButton
    : public NodeFor<MinimalAuxiliaryOpenerButton, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::minimal::MinimalAuxiliaryOpenerButton",
      MinimalAuxiliaryOpenerButton, ui::UiPushButton, 0)

 protected:
  MinimalAuxiliaryOpenerButton() = default;

 public:
  explicit MinimalAuxiliaryOpenerButton(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_title), AE_MMBR(row_list), AE_MMBR(application))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_title, row_list, application);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_title, row_list, application);
  }

  ui::UiLocalizationString::ptr window_title;
  MinimalAppList::ptr row_list;
  ae::ObjPtr<MinimalApplication> application;

  void HandlePushButtonClicked() override;
};

class MinimalAuxiliaryCloserButton
    : public NodeFor<MinimalAuxiliaryCloserButton, ui::UiPushButton> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::minimal::MinimalAuxiliaryCloserButton",
      MinimalAuxiliaryCloserButton, ui::UiPushButton, 0)

 protected:
  MinimalAuxiliaryCloserButton() = default;

 public:
  explicit MinimalAuxiliaryCloserButton(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_title), AE_MMBR(application))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_title, application);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_title, application);
  }

  ui::UiLocalizationString::ptr window_title;
  ae::ObjPtr<MinimalApplication> application;

  void HandlePushButtonClicked() override;
};

struct MinimalAuxiliaryWindowState {
  ui::UiDesktopWindow::ptr window;
  MinimalApplyButton::ptr apply_button;
  MinimalAppList::ptr row_list;
  MinimalAuxiliaryPresenters presenters;
  MinimalAuxiliaryCloserButton::ptr closer;

  AE_REFLECT_MEMBERS(window, apply_button, row_list, presenters, closer)
};

class MinimalOpenAuxiliaryWindowEvent;
class MinimalCloseAuxiliaryWindowEvent;
class MinimalViewportEvent;
class MinimalDesktopEnvironmentEvent;

ae::ObjId MinimalApplicationRootId();
ae::ObjId MinimalMainWindowId();
ae::ObjId MinimalPresenterId();

void EnsureMinimalApplicationRegistration();

struct MinimalViewportState {
  std::int32_t width;
  std::int32_t height;
  std::int32_t density_dpi;
  bool hosted;
  bool single_page;
  AE_REFLECT_MEMBERS(width, height, density_dpi, hosted, single_page)
};

class MinimalApplication : public NodeFor<MinimalApplication> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalApplication",
                           MinimalApplication, Node, 0)

 protected:
  MinimalApplication() = default;

 public:
  explicit MinimalApplication(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(main_window), AE_MMBR(apply_button),
                    AE_MMBR(row_list), AE_MMBR(presenters),
                    AE_MMBR(auxiliary_opener), AE_MMBR(auxiliary),
                    AE_MMBR(auxiliary_presented_open), AE_MMBR(viewport))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, main_window, apply_button, row_list, presenters,
        auxiliary_opener, auxiliary, auxiliary_presented_open, viewport);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, main_window, apply_button, row_list, presenters,
        auxiliary_opener, auxiliary, auxiliary_presented_open, viewport);
  }

  ui::UiDesktopWindow::ptr main_window;
  MinimalApplyButton::ptr apply_button;
  MinimalAppList::ptr row_list;
  MinimalMainPresenters presenters;
  MinimalAuxiliaryOpenerButton::ptr auxiliary_opener;
  MinimalAuxiliaryWindowState auxiliary;
  bool auxiliary_presented_open;
  MinimalViewportState viewport;

  void Apply(MinimalOpenAuxiliaryWindowEvent const& event);
  void Apply(MinimalCloseAuxiliaryWindowEvent const& event);
  void Apply(MinimalViewportEvent const& event);
  void Apply(MinimalDesktopEnvironmentEvent const& event);
  void SubmitDesktopEnvironment(std::int32_t dpi);
  void SubmitViewport(std::int32_t width, std::int32_t height,
                      std::int32_t dpi, bool requires_single_page);
  void ReflowWindow(ui::UiDesktopWindow& window);
  void LayoutHostedWindows();
  ae::ObjId ActivePageId() const {
    return auxiliary_presented_open ? auxiliary.window.id() : main_window.id();
  }

  void LoadAuxiliaryPresentation();
  void OpenAuxiliaryWindowImpl();
  void CloseAuxiliaryWindowImpl();

};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APPLICATION_H_
