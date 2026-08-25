#ifndef APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_
#define APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_

#include <string>

#include "chat_presentation.h"
#include "model/chat.h"
#include "model/chat_entry.h"

namespace apptraverse::examples {

// Platform-neutral UTF-8 transcript from a presentation snapshot.
// Prefer calling this on the UI thread under model_mutex (stack-local only;
// do not PostMessage snapshot payloads across threads).
inline std::string FormatChatPresentationUtf8(
    chat::ChatPresentationSnapshot const& snapshot) {
  std::string text;
  for (auto const& item : snapshot.timeline) {
    if (item.kind == chat::ChatTimelineItemKind::kJoined) {
      if (item.author.display_name.empty()) {
        continue;
      }
      text += "* ";
      text += item.author.display_name;
      text += " joined\n";
    } else if (item.kind == chat::ChatTimelineItemKind::kMessage) {
      text += item.author.display_name;
      text += ": ";
      text += item.text;
      text += "\n";
    }
  }
  return text;
}

// CRLF transcript suitable for Win32 multiline EDIT controls.
inline std::string FormatChatPresentationWin32Utf8(
    chat::ChatPresentationSnapshot const& snapshot) {
  auto text = FormatChatPresentationUtf8(snapshot);
  std::string crlf;
  crlf.reserve(text.size() + 8);
  for (char ch : text) {
    if (ch == '\n') {
      crlf += "\r\n";
    } else if (ch != '\r') {
      crlf.push_back(ch);
    }
  }
  return crlf;
}

// Participants panel UTF-8 (CRLF). Stack-local use under model_mutex only.
inline std::string FormatParticipantsWin32Utf8(
    chat::ChatPresentationSnapshot const& snapshot) {
  std::string utf8 = "Participants\r\n";
  auto glyph = [](chat::ChatPeerStatusView const& peer) -> char const* {
    if (peer.is_local) {
      return "\xE2\x97\x8F";
    }
    switch (peer.presence) {
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
  for (auto const& peer : snapshot.peers) {
    utf8 += "\r\n";
    std::string name = peer.display_name.empty() ? peer.remote_uid
                                                 : peer.display_name;
    if (name.empty()) {
      name = peer.is_local ? "You" : "Peer";
    }
    utf8 += name;
    if (peer.is_host) {
      utf8 += " (Host)";
    }
    if (peer.is_local) {
      utf8 += " (You)";
    }
    utf8 += " ";
    utf8 += glyph(peer);
    utf8 += " ";
    if (peer.is_local) {
      utf8 += chat::LocalPresenceStatusName(snapshot.local_presence);
    } else {
      utf8 += chat::PeerPresenceStatusName(peer.presence);
    }
  }
  return utf8;
}

// Temporary helper for sync tests that still hold a Chat::ptr.
// Production Windows/Android paths should use FormatChatPresentationUtf8.
inline std::string FormatChatTranscriptUtf8(Chat::ptr const& chat) {
  if (!chat.is_valid()) {
    return {};
  }
  chat.Load();
  if (!chat.is_loaded()) {
    return {};
  }

  std::string text;
  for (auto const& entry : chat->entries) {
    if (!entry.client.is_valid()) {
      continue;
    }
    auto client = entry.client;
    client.Load();
    if (!client.is_loaded()) {
      continue;
    }
    if (entry.kind == ChatEntryKind::kJoined) {
      text += "* ";
      text += client->name;
      text += " joined\n";
    } else if (entry.kind == ChatEntryKind::kMessage) {
      text += client->name;
      text += ": ";
      text += entry.text;
      text += "\n";
    }
  }
  return text;
}

}  // namespace apptraverse::examples

#endif  // APPTRAVERSE_EXAMPLES_CHAT_TRANSCRIPT_H_