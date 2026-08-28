#include "chat_presentation.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace apptraverse {
namespace {

#if defined(_WIN32)
std::tm LocalTimeTm(std::time_t sec) {
  std::tm out{};
  localtime_s(&out, &sec);
  return out;
}
#else
std::tm LocalTimeTm(std::time_t sec) {
  std::tm out{};
  localtime_r(&sec, &out);
  return out;
}
#endif

}  // namespace

std::string FormatUnixMsLocalTime(std::int64_t unix_ms) {
  if (unix_ms <= 0) {
    return {};
  }
  auto const sec = static_cast<std::time_t>(unix_ms / 1000);
  auto const ms = static_cast<int>(unix_ms % 1000);
  std::tm tm = LocalTimeTm(sec);
  std::ostringstream out;
  out << std::setfill('0') << std::setw(2) << tm.tm_hour << ':' << std::setw(2)
      << tm.tm_min << ':' << std::setw(2) << tm.tm_sec << '.' << std::setw(3)
      << ms;
  return out.str();
}

std::string FormatChatJoinDisplayLine(std::string const& author) {
  std::string name = author.empty() ? "Unknown" : author;
  return name + " joined the chat";
}

std::string FormatChatMessageDisplayLine(std::string const& author,
                                         std::string const& body,
                                         std::int64_t sent_at_unix_ms) {
  std::string name = author.empty() ? "Unknown" : author;
  auto const time = FormatUnixMsLocalTime(sent_at_unix_ms);
  if (time.empty()) {
    return name + ": " + body;
  }
  return "[" + time + "] " + name + ": " + body;
}

std::string AppendUiLatencySuffix(std::string const& line, double latency_ms) {
  std::ostringstream out;
  out << line << "  [UI " << std::fixed << std::setprecision(1) << latency_ms
      << " ms]";
  return out.str();
}

ChatPresentationSnapshot BuildChatPresentationSnapshot(
    ChatRoom const& room, ChatPresentationOptions const& options) {
  ChatPresentationSnapshot snapshot;
  for (auto const& item : room.feed) {
    if (!item.is_valid()) {
      continue;
    }
    item.Load();
    ChatFeedPresentationItem row;
    row.kind = item->kind;
    row.sent_at_unix_ms = item->sent_at_unix_ms;
    row.source_event_obj_id = item->source_event_obj_id;
    if (item->client.is_valid()) {
      item->client.Load();
      row.author_name = item->client->DisplayNameBytes();
      row.author_uid = item->client->AetherUidText();
    }
    row.is_local_message = !options.local_aether_uid.empty() &&
                           row.author_uid == options.local_aether_uid &&
                           item->kind == kChatFeedKindMessage;
    if (item->kind == kChatFeedKindJoin) {
      row.display_line = FormatChatJoinDisplayLine(row.author_name);
    } else {
      if (item->body.is_valid()) {
        item->body.Load();
        row.text = item->body->bytes;
      }
      row.display_line = FormatChatMessageDisplayLine(
          row.author_name, row.text, row.sent_at_unix_ms);
      if (row.is_local_message && options.latency_ms_for_event &&
          row.source_event_obj_id != 0) {
        if (auto latency =
                options.latency_ms_for_event(row.source_event_obj_id)) {
          row.display_line =
              AppendUiLatencySuffix(row.display_line, *latency);
        }
      }
    }
    snapshot.feed.push_back(std::move(row));
  }
  for (auto const& client : room.clients) {
    if (!client.is_valid()) {
      continue;
    }
    client.Load();
    ChatContactPresentationItem row;
    row.client_obj_id = client.id().id();
    row.display_name = client->DisplayNameBytes();
    row.aether_uid = client->AetherUidText();
    row.online = client->online;
    snapshot.contacts.push_back(std::move(row));
  }
  return snapshot;
}

}  // namespace apptraverse
