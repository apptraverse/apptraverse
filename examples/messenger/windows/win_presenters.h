#ifndef APPTRAVERSE_MESSENGER_WIN_PRESENTERS_H_
#define APPTRAVERSE_MESSENGER_WIN_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cstdint>

#include "apptraverse/object_macros.h"

#include "desktop_surface_presenter.h"

namespace apptraverse {

inline wchar_t const kMessengerWindowClass[] = L"AppTraverseMessengerWindow";

inline constexpr int kOwnUidEditId = 1001;
inline constexpr int kCopyButtonId = 1002;
inline constexpr int kPeerUidEditId = 1003;
inline constexpr int kTranscriptEditId = 1004;
inline constexpr int kDraftEditId = 1005;

void RegisterMessengerWin32Classes();
void UnregisterMessengerWin32Classes();
void EnsureWin32SurfacePresenterRegistration();

class Win32SurfacePresenter : public DesktopSurfacePresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::messenger::Win32SurfacePresenter",
      Win32SurfacePresenter, DesktopSurfacePresenter, 0)

 protected:
  Win32SurfacePresenter() = default;

 public:
  explicit Win32SurfacePresenter(ae::ObjProp prop)
      : DesktopSurfacePresenter{prop} {}

  AE_OBJECT_REFLECT()

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  void QueueCurrentBounds();

  void ConfirmPeerUid();
  void ConfirmSendDraft();

  HWND hwnd{nullptr};
  HWND own_uid_edit{nullptr};
  HWND copy_button{nullptr};
  HWND peer_uid_edit{nullptr};
  HWND transcript_edit{nullptr};
  HWND draft_edit{nullptr};

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);

 private:
  void LayoutControls();
  void SyncControlsFromModel();
  void CopyOwnUid();
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_WIN_PRESENTERS_H_
