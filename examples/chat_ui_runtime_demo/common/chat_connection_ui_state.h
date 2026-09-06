#ifndef APPTRAVERSE_CHAT_CONNECTION_UI_STATE_H_
#define APPTRAVERSE_CHAT_CONNECTION_UI_STATE_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace apptraverse {

enum class ChatConnectionUiStatus : std::uint8_t {
  NotConnected = 0,
  Connecting = 1,
  Connected = 2,
  Disconnected = 3,
  InvalidId = 4,
};

struct ChatConnectionUiState {
  ChatConnectionUiStatus status{ChatConnectionUiStatus::NotConnected};
  // While Connecting: seconds since P2P OpenPeer click.
  double elapsed_sec{0.0};
  bool connect_enabled{true};
};

struct ChatRuntimeDiagUiState {
  std::size_t journal_count{0};
  std::size_t pending_count{0};
  bool peer_connected{false};
};

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

inline std::string FormatConnectionStatusText(
    ChatConnectionUiState const& state) {
  switch (state.status) {
    case ChatConnectionUiStatus::NotConnected:
      return "Not connected";
    case ChatConnectionUiStatus::Connecting: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "Connecting... %.1f s",
                    state.elapsed_sec);
      return buf;
    }
    case ChatConnectionUiStatus::Connected:
      return "Connected";
    case ChatConnectionUiStatus::Disconnected:
      return "Disconnected";
    case ChatConnectionUiStatus::InvalidId:
      return "Invalid Aether ID";
  }
  return "Not connected";
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_CONNECTION_UI_STATE_H_
