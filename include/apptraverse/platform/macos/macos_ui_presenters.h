#ifndef APPTRAVERSE_PLATFORM_MACOS_MACOS_UI_PRESENTERS_H_
#define APPTRAVERSE_PLATFORM_MACOS_MACOS_UI_PRESENTERS_H_

#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::macos {

class MacDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      ::apptraverse::ui::native_schema::kWindow,
      MacDesktopWindowPresenter, UiDesktopWindowPresenter, 0)

 protected:
  MacDesktopWindowPresenter() = default;

 public:
  explicit MacDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  void ApplyRequestedFrameFromMirror() override;
  void ReportNativeFrameToModel(std::int32_t x, std::int32_t y,
                                std::int32_t client_width,
                                std::int32_t client_height,
                                bool updates_restore_geometry) override;
  void SubmitScreenFromNative(std::int32_t x, std::int32_t y,
                              std::int32_t width, std::int32_t height) override;
  void ReportNativeScreenToModel(std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height);
};

class MacPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      ::apptraverse::ui::native_schema::kPushButton,
      MacPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  MacPushButtonPresenter() = default;

 public:
  explicit MacPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
};

class MacListContainerPresenter : public UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      ::apptraverse::ui::native_schema::kList,
      MacListContainerPresenter, UiListContainerPresenter, 0)

 protected:
  MacListContainerPresenter() = default;

 public:
  explicit MacListContainerPresenter(ae::ObjProp prop)
      : UiListContainerPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
};

void EnsureMacUiPresenterRegistration();

// Native presentation operations addressed by logical model-window identity.
// They operate only on the current, activated UI graph, never historical nodes.
void FocusMacDesktopWindow(ae::Obj& ui_root, ae::ObjId window_id);
std::vector<std::uint32_t> CaptureMacDesktopZOrder(ae::Obj& ui_root);
void RestoreMacDesktopZOrder(ae::Obj& ui_root,
                            std::vector<std::uint32_t> const& top_to_bottom);

}  // namespace apptraverse::ui::macos

#endif  // APPTRAVERSE_PLATFORM_MACOS_MACOS_UI_PRESENTERS_H_
