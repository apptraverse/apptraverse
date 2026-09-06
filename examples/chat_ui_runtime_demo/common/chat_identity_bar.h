#ifndef CHAT_IDENTITY_BAR_H_
#define CHAT_IDENTITY_BAR_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "apptraverse/runtime_lifecycle.h"
#include "chat_model.h"

namespace chat {

inline constexpr char const* kIdentityBarRegistering =
    "Registering in network...";
inline constexpr char const* kIdentityBarNoInterface = "No network interface";
inline constexpr char const* kIdentityBarNoInternet = "Internet unavailable";
inline constexpr wchar_t const* kIdentityBarEnterRoomCue = L"Enter room UID";

struct IdentityBarView {
  std::string field_text;
  bool field_readonly{true};
  bool copy_visible{false};
  bool copy_enabled{false};
  bool join_visible{false};
  bool join_enabled{false};
  bool show_edit_cue{false};
};

inline IdentityBarView ProjectIdentityBar(
    ChatRole role, apptraverse::NetworkState const& network,
    apptraverse::AetherRegistrationState const& aether) {
  IdentityBarView view;
  bool const host = role == ChatRole::Host;
  view.copy_visible = host;
  view.join_visible = !host;

  if (aether.IsRegisteredForCurrentRun()) {
    if (host) {
      view.field_text = aether.CurrentUid();
      view.field_readonly = true;
      view.copy_enabled = !view.field_text.empty();
    } else {
      view.field_text.clear();
      view.field_readonly = false;
      view.join_enabled = true;
      view.show_edit_cue = true;
    }
    return view;
  }

  view.field_readonly = true;
  view.copy_enabled = false;
  view.join_enabled = false;
  switch (network.GetAvailability()) {
    case apptraverse::NetworkAvailability::kInterfaceUnavailable:
      view.field_text = kIdentityBarNoInterface;
      break;
    case apptraverse::NetworkAvailability::kInternetUnavailable:
      view.field_text = kIdentityBarNoInternet;
      break;
    case apptraverse::NetworkAvailability::kInitializing:
    case apptraverse::NetworkAvailability::kAvailable:
      view.field_text = kIdentityBarRegistering;
      break;
  }
  return view;
}

inline bool LooksLikeAetherUid(std::string_view text) {
  if (text.size() != 36) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    char const c = text[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') {
        return false;
      }
      continue;
    }
    bool const hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F');
    if (!hex) {
      return false;
    }
  }
  return true;
}

}  // namespace chat

#endif  // CHAT_IDENTITY_BAR_H_
