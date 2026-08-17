#ifndef APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <functional>
#include <string>

#include "model/chat_presenter.h"
#include "apptraverse/object_macros.h"
#include "../common/chat_presentation.h"
#include "../common/chat_transcript.h"
#include "win_add_peer_dialog.h"

namespace apptraverse {

class WinChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(WinChatPresenter, ChatPresenter, 0)

 protected:
  WinChatPresenter() = default;

 public:
  explicit WinChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()

  // Runtime-only UI wiring. Not reflected / not serialized.
  void SetPeerUi(std::string local_aether_uid,
                 std::function<AddPeerUiResult(std::string const&)> add_peer) {
    local_aether_uid_ = std::move(local_aether_uid);
    add_peer_ = std::move(add_peer);
  }

  void SetSubmitTextHandler(std::function<bool(std::string)> handler) {
    submit_text_ = std::move(handler);
  }

  void CreateControls(HWND parent) {
    parent_ = parent;
    transcript_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)),
        GetModuleHandleW(nullptr), nullptr);

    edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)),
        GetModuleHandleW(nullptr), nullptr);

    send_ = CreateWindowExW(
        0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
        0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(3)),
        GetModuleHandleW(nullptr), nullptr);

    add_ = CreateWindowExW(
        0, L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(4)),
        GetModuleHandleW(nullptr), nullptr);
  }

  void Layout(int width, int height) {
    int const margin = 8;
    int const edit_h = 28;
    int const send_w = 80;
    int const add_w = 36;
    int const bottom = height - margin - edit_h;
    int const transcript_h = bottom - margin;
    if (transcript_ != nullptr) {
      MoveWindow(transcript_, margin, margin, width - 2 * margin,
                 transcript_h > 0 ? transcript_h : 0, TRUE);
    }
    int const edit_w = width - 4 * margin - send_w - add_w;
    if (edit_ != nullptr) {
      MoveWindow(edit_, margin, bottom, edit_w > 0 ? edit_w : 0, edit_h, TRUE);
    }
    if (send_ != nullptr) {
      MoveWindow(send_, width - 2 * margin - add_w - send_w, bottom, send_w,
                 edit_h, TRUE);
    }
    if (add_ != nullptr) {
      MoveWindow(add_, width - margin - add_w, bottom, add_w, edit_h, TRUE);
    }
  }

  void OnSendClicked() {
    if (edit_ == nullptr || !submit_text_) {
      return;
    }
    int const len = GetWindowTextLengthW(edit_);
    std::wstring wide;
    wide.resize(static_cast<std::size_t>(len) + 1);
    int const copied = GetWindowTextW(edit_, &wide[0], len + 1);
    wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    while (!wide.empty() &&
           (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' ')) {
      wide.pop_back();
    }
    if (wide.empty()) {
      return;
    }

    if (submit_text_(WideToUtf8(wide))) {
      SetWindowTextW(edit_, L"");
    }
  }

  void OnAddClicked() {
    if (local_aether_uid_.empty() || !add_peer_) {
      return;
    }
    ShowAddPeerDialog(parent_, local_aether_uid_, add_peer_);
  }

  void RenderPresentation(examples::ChatPresentationSnapshot const& snapshot) {
    if (transcript_ == nullptr) {
      return;
    }
    auto const utf8 = examples::FormatChatPresentationUtf8(snapshot);
    std::wstring text = Utf8ToWide(utf8);
    // EDIT control expects CRLF line endings on Windows.
    std::wstring crlf;
    crlf.reserve(text.size() + 8);
    for (wchar_t ch : text) {
      if (ch == L'\n') {
        crlf += L"\r\n";
      } else if (ch != L'\r') {
        crlf.push_back(ch);
      }
    }
    SetWindowTextW(transcript_, crlf.c_str());
    SendMessageW(transcript_, EM_SETSEL, static_cast<WPARAM>(crlf.size()),
                 static_cast<LPARAM>(crlf.size()));
    SendMessageW(transcript_, EM_SCROLLCARET, 0, 0);
  }

  // Deprecated: production uses RenderPresentation only.
  void RefreshTranscript() {}

  HWND transcript() const { return transcript_; }
  HWND edit() const { return edit_; }
  HWND send() const { return send_; }
  HWND add() const { return add_; }

 private:
  static std::string WideToUtf8(std::wstring const& wide) {
    if (wide.empty()) {
      return {};
    }
    int const size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                         static_cast<int>(wide.size()), nullptr,
                                         0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
  }

  static std::wstring Utf8ToWide(std::string const& utf8) {
    if (utf8.empty()) {
      return {};
    }
    int const size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                            static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), size);
    return out;
  }

  HWND parent_{nullptr};
  HWND transcript_{nullptr};
  HWND edit_{nullptr};
  HWND send_{nullptr};
  HWND add_{nullptr};
  std::string local_aether_uid_;
  std::function<AddPeerUiResult(std::string const&)> add_peer_;
  std::function<bool(std::string)> submit_text_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_
