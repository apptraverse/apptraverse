#ifndef APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_
#define APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstdint>

#include "apptraverse/object_macros.h"

#include "apptraverse/surfaces/surface_button_presenter.h"
#include "apptraverse/surfaces/surface_language_button_presenter.h"
#include "apptraverse/surfaces/surface_window_presenter.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {

inline wchar_t const kSurfacesWindowClass[] = L"AppTraverseSurfacesWindow";
inline constexpr int kSurfaceChromeMargin = 12;
inline constexpr int kSurfaceAddButtonLayoutX = kSurfaceChromeMargin;
inline constexpr int kSurfaceAddButtonLayoutY = kSurfaceChromeMargin;
inline constexpr int kSurfaceAddButtonLayoutWidth = 88;
inline constexpr int kSurfaceAddButtonLayoutHeight = 28;
inline constexpr int kSurfaceAddButtonId = 1001;
inline constexpr int kSurfaceCloseButtonId = 1002;
inline constexpr int kSurfaceLanguageButtonId = 1003;

void RegisterSurfacesWin32Classes();
void UnregisterSurfacesWin32Classes();
void EnsureWin32SurfacesPresenterRegistration();

class Presenter;

struct SurfaceChildBnClickedTarget {
  HWND child_hwnd{nullptr};
  Presenter* presenter{nullptr};
  std::uint32_t control_id{0};
};

bool TryParseSurfaceChildBnClicked(HWND parent_hwnd, WPARAM wparam, LPARAM lparam,
                                   SurfaceChildBnClickedTarget* out);

bool DispatchChildCommand(HWND parent_hwnd, WPARAM wparam, LPARAM lparam);

void ComputeSurfaceChromeButtonLayout(SurfaceWindowPresenter const& window,
                                      std::uint32_t control_id, int* x,
                                      int* y, int* w, int* h);

class Win32SurfaceWindowPresenter : public SurfaceWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::Win32SurfaceWindowPresenter",
      Win32SurfaceWindowPresenter, SurfaceWindowPresenter, 0)

 protected:
  Win32SurfaceWindowPresenter() = default;

 public:
  explicit Win32SurfaceWindowPresenter(ae::ObjProp prop)
      : SurfaceWindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  void QueueCurrentBounds();

  using ChildCommandHandler = bool (*)(Win32SurfaceWindowPresenter* window,
                                       WPARAM wparam, LPARAM lparam);
  ChildCommandHandler child_command_handler{nullptr};

  HWND hwnd{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);

 private:
  void SyncWindowTitle();
};

class Win32SurfaceButtonPresenter : public SurfaceButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::Win32SurfaceButtonPresenter",
      Win32SurfaceButtonPresenter, SurfaceButtonPresenter, 0)

 protected:
  Win32SurfaceButtonPresenter() = default;

 public:
  explicit Win32SurfaceButtonPresenter(ae::ObjProp prop)
      : SurfaceButtonPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  void NotifyClickedFromUi() override;

  HWND hwnd{nullptr};

 private:
  void ApplyLayout();
  void SyncCaption();
};

class Win32SurfaceLanguageButtonPresenter
    : public SurfaceLanguageButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::Win32SurfaceLanguageButtonPresenter",
      Win32SurfaceLanguageButtonPresenter, SurfaceLanguageButtonPresenter, 0)

 protected:
  Win32SurfaceLanguageButtonPresenter() = default;

 public:
  explicit Win32SurfaceLanguageButtonPresenter(ae::ObjProp prop)
      : SurfaceLanguageButtonPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd{nullptr};

 private:
  void ApplyLayout();
  void SyncCaption();
};

Win32SurfaceWindowPresenter::ptr ResolveWin32WindowPresenter(
    Surface const& surface);

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN_PRESENTERS_H_
