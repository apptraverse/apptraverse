#ifndef APPTRAVERSE_EXAMPLES_WIN_PARTICIPANT_LIST_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_WIN_PARTICIPANT_LIST_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <functional>
#include <string>
#include <vector>

#include "aether/types/uid.h"

#include "../common/chat_peer_schedule.h"
#include "../common/room_control.h"
#include "../common/startup_trace.h"

namespace apptraverse {

// Header-only Win32 participant list (~200px right column).
// PresentLive updates HWND while the caller holds model shared_lock.
class WinParticipantListPresenter {
 public:
  using GetPeerPresenceFn =
      std::function<chat::PeerPresenceStatus(ae::Uid const&)>;

  void CreateControls(HWND parent) {
    parent_ = parent;
    list_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL,
        0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(20)),
        GetModuleHandleW(nullptr), nullptr);
  }

  // Right column ~200px; returns left content width after layout.
  // top_offset reserves space for the room host/join bar above.
  int Layout(int width, int height, int margin, int top_offset,
             int bottom_reserve) {
    int const list_w = 200;
    int const list_h = height - top_offset - bottom_reserve - margin;
    int const x = width - margin - list_w;
    if (list_ != nullptr) {
      MoveWindow(list_, x > margin ? x : margin, top_offset,
                 list_w, list_h > 0 ? list_h : 0, TRUE);
    }
    int const content_w = width - 3 * margin - list_w;
    return content_w > 0 ? content_w : 0;
  }

  void PresentLive(std::vector<chat::RoomParticipantDesc> const& participants,
                   ae::Uid const& host_uid, std::uint32_t local_client_obj_id,
                   GetPeerPresenceFn const& get_peer_presence,
                   chat::LocalPresenceStatus local_presence) {
    examples::AssertUiThread("WinParticipantListPresenter::PresentLive");
    if (list_ == nullptr) {
      return;
    }
    std::string utf8 = "Participants\r\n";
    auto glyph = [](bool is_local,
                    chat::PeerPresenceStatus presence) -> char const* {
      if (is_local) {
        return "\xE2\x97\x8F";
      }
      switch (presence) {
        case chat::PeerPresenceStatus::kOnline:
          return "\xE2\x97\x8F";
        case chat::PeerPresenceStatus::kOffline:
          return "\xE2\x97\x8B";
        case chat::PeerPresenceStatus::kNotRunning:
          return "\xE2\x80\x94";
        case chat::PeerPresenceStatus::kUnknown:
          return "?";
      }
      return "?";
    };
    auto local_as_peer = [](chat::LocalPresenceStatus s) {
      switch (s) {
        case chat::LocalPresenceStatus::kOnline:
          return chat::PeerPresenceStatus::kOnline;
        case chat::LocalPresenceStatus::kOffline:
          return chat::PeerPresenceStatus::kOffline;
        case chat::LocalPresenceStatus::kConnecting:
          return chat::PeerPresenceStatus::kUnknown;
      }
      return chat::PeerPresenceStatus::kUnknown;
    };

    for (auto const& part : participants) {
      utf8 += "\r\n";
      bool const is_local =
          part.client_obj_id != 0 && part.client_obj_id == local_client_obj_id;
      bool const is_host = !host_uid.empty() && part.uid == host_uid;
      std::string name =
          part.display_name.empty() ? std::string{"Peer"} : part.display_name;
      utf8 += name;
      if (is_host) {
        utf8 += " (Host)";
      }
      if (is_local) {
        utf8 += " (You)";
      }
      utf8 += " ";
      auto const presence =
          is_local ? local_as_peer(local_presence)
                   : (get_peer_presence ? get_peer_presence(part.uid)
                                        : chat::PeerPresenceStatus::kUnknown);
      utf8 += glyph(is_local, presence);
      utf8 += " ";
      if (is_local) {
        utf8 += chat::LocalPresenceStatusName(local_presence);
      } else {
        utf8 += chat::PeerPresenceStatusName(presence);
      }
    }
    if (utf8 == last_utf8_) {
      return;
    }
    SetWindowTextW(list_, Utf8ToWide(utf8).c_str());
    last_utf8_ = std::move(utf8);
    if (local_presence == chat::LocalPresenceStatus::kOnline &&
        examples::StartupOnceFlag(examples::StartupFlagUiOnlinePresented())) {
      examples::StartupTrace(
          "UI_ONLINE_PRESENTED",
          "via=WinParticipantListPresenter::PresentLive");
    }
  }

  HWND hwnd() const { return list_; }

 private:
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
  HWND list_{nullptr};
  std::string last_utf8_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_PARTICIPANT_LIST_PRESENTER_H_
