#ifndef APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <string>

#include "apptraverse/chat_entry.h"
#include "apptraverse/chat_presenter.h"

namespace apptraverse {

class WinChatPresenter : public ChatPresenter {
  AE_OBJECT(WinChatPresenter, ChatPresenter, 0)

 protected:
  WinChatPresenter() = default;

 public:
  explicit WinChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void CreateControls(HWND parent) {
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

    RefreshTranscript();
  }

  void Layout(int width, int height) {
    int const margin = 8;
    int const edit_h = 28;
    int const send_w = 80;
    int const bottom = height - margin - edit_h;
    int const transcript_h = bottom - margin;
    if (transcript_ != nullptr) {
      MoveWindow(transcript_, margin, margin, width - 2 * margin,
                 transcript_h > 0 ? transcript_h : 0, TRUE);
    }
    if (edit_ != nullptr) {
      MoveWindow(edit_, margin, bottom, width - 3 * margin - send_w, edit_h,
                 TRUE);
    }
    if (send_ != nullptr) {
      MoveWindow(send_, width - margin - send_w, bottom, send_w, edit_h, TRUE);
    }
  }

  void OnSendClicked() {
    if (edit_ == nullptr) {
      return;
    }
    int const len = GetWindowTextLengthW(edit_);
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    if (len > 0) {
      GetWindowTextW(edit_, wide.data(), len + 1);
    }
    while (!wide.empty() &&
           (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' ')) {
      wide.pop_back();
    }
    if (wide.empty()) {
      return;
    }

    SubmitText(WideToUtf8(wide));
    SetWindowTextW(edit_, L"");
    RefreshTranscript();
  }

  void RefreshTranscript() {
    if (transcript_ == nullptr || !chat.is_valid()) {
      return;
    }
    chat.Load();
    std::wstring text;
    for (auto const& entry : chat->entries) {
      auto loaded = entry;
      loaded.Load();
      if (!loaded.is_loaded()) {
        continue;
      }
      if (loaded->GetClassId() == JoinClientEntry::kClassId) {
        auto& join = static_cast<JoinClientEntry&>(*loaded);
        join.client.Load();
        text += L"* ";
        text += Utf8ToWide(join.client->name);
        text += L" joined\r\n";
      } else if (loaded->GetClassId() == MessageEntry::kClassId) {
        auto& message = static_cast<MessageEntry&>(*loaded);
        message.author.Load();
        text += Utf8ToWide(message.author->name);
        text += L": ";
        text += Utf8ToWide(message.text);
        text += L"\r\n";
      }
    }
    SetWindowTextW(transcript_, text.c_str());
    SendMessageW(transcript_, EM_SETSEL, static_cast<WPARAM>(text.size()),
                 static_cast<LPARAM>(text.size()));
    SendMessageW(transcript_, EM_SCROLLCARET, 0, 0);
  }

  HWND transcript() const { return transcript_; }
  HWND edit() const { return edit_; }
  HWND send() const { return send_; }

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

  HWND transcript_{nullptr};
  HWND edit_{nullptr};
  HWND send_{nullptr};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_CHAT_PRESENTER_H_
