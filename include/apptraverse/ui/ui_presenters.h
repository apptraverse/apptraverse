#ifndef APPTRAVERSE_UI_UI_PRESENTERS_H_
#define APPTRAVERSE_UI_UI_PRESENTERS_H_

#include <optional>

#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_window_presenter.h"

#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::ui {

class UiDesktopWindowPresenter : public UiWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiDesktopWindowPresenter",
                           UiDesktopWindowPresenter, UiWindowPresenter, 0)

 protected:
  UiDesktopWindowPresenter() = default;

 public:
  explicit UiDesktopWindowPresenter(ae::ObjProp prop)
      : UiWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  UiDesktopWindow::ptr desktop_window() const {
    return UiDesktopWindow::ptr::MakeFromThis(
        static_cast<UiDesktopWindow*>(&*window));
  }

  // Native frame report: presenter → ModelObjectProxy → UiDesktopWindow Event.
  void SubmitFrameFromNative(std::int32_t x, std::int32_t y,
                             std::int32_t client_width,
                             std::int32_t client_height,
                             bool updates_restore_geometry = false);

  virtual void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                        std::int32_t client_width,
                                        std::int32_t client_height,
                                        bool updates_restore_geometry);

  void SubmitScreenFromNative(std::int32_t x, std::int32_t y,
                              std::int32_t width, std::int32_t height);

  virtual void ApplyRequestedFrameFromMirror() {}
};

class UiListContainerPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiListContainerPresenter",
                           UiListContainerPresenter, Presenter, 0)

 protected:
  UiListContainerPresenter() = default;

 public:
  explicit UiListContainerPresenter(ae::ObjProp prop)
      : Presenter{prop}, notify_list_selection_change{false} {}

  AE_OBJECT_REFLECT(AE_MMBR(container), AE_MMBR(window_presenter),
                    AE_MMBR(layout_x), AE_MMBR(layout_y),
                    AE_MMBR(layout_width), AE_MMBR(layout_height),
                    AE_MMBR(control_id), AE_MMBR(notify_list_selection_change))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, container, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id, notify_list_selection_change);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, container, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id, notify_list_selection_change);
  }

  UiListContainer::ptr container;
  ae::ObjPtr<UiWindowPresenter> window_presenter;
  std::int32_t layout_x;
  std::int32_t layout_y;
  std::int32_t layout_width;
  std::int32_t layout_height;
  std::uint32_t control_id;
  bool notify_list_selection_change;

  bool ReadyForPresentation() const override;
  void OnModelChanged() override;

  // List UI input → ModelObjectProxy → UiListContainer model handlers (override
  // Submit* in app adapters such as WindowCatalog; do not mutate mirror here).
  virtual void SubmitListRowActivatedFromUi(std::size_t row_index);
  virtual void SubmitListRowSelectionChangedFromUi(std::size_t row_index);

  virtual void OnListRowActivated(std::size_t row_index);
  virtual void OnListRowSelectionChanged(std::size_t row_index);

 protected:
  // Resolve against the displayed mirror before posting; no model pointer crosses domains.
  std::optional<ae::ObjId> RowIdAtUiIndex(std::size_t row_index) const;

 public:
  virtual std::string ResolveListRowPrimaryText(ui::UiListRow::ptr const& row,
                                               int language_index) const;

  virtual std::vector<std::string> ResolveListRowPrimaryTexts(
      int language_index) const;
};

void EnsureUiPresenterRegistration();

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PRESENTERS_H_
