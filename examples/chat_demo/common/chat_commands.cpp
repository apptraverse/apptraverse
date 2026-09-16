#include "chat_commands.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "apptraverse/runtime_node.h"

namespace apptraverse::example::chat_demo {
namespace {

std::string TrimAsciiWhitespace(std::string_view sv) {
  auto start = sv.begin();
  while (start != sv.end() && std::isspace(static_cast<unsigned char>(*start))) {
    ++start;
  }
  auto end = sv.end();
  while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string(start, end);
}

}  // namespace

bool ConfigureDemoRole(ChatWorkspace& workspace, DemoRole role,
                       PersistLocalState const& persist) {
  if (role != DemoRole::kHost && role != DemoRole::kClient) {
    return false;
  }
  if (workspace.demo_role == role) {
    return true;
  }
  if (workspace.demo_role != DemoRole::kUnconfigured) {
    return false;
  }
  auto event =
      DemoRoleConfiguredEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  event->role = role;
  workspace.Commit(event);
  if (persist) {
    persist();
  }
  return true;
}

bool SetHostUidInput(ChatWorkspace& workspace, std::string const& text,
                     PersistLocalState const& persist) {
  if (workspace.demo_role != DemoRole::kClient) {
    return false;
  }
  if (workspace.host_uid_input == text) {
    return true;
  }
  auto event =
      HostUidInputChangedEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  event->text = text;
  workspace.Commit(event);
  if (persist) {
    persist();
  }
  return true;
}

std::string ConversationDisplayName(DemoRole local_role,
                                    std::string const& peer_uid) {
  std::string const suffix =
      peer_uid.size() >= 8 ? peer_uid.substr(0, 8) : peer_uid;
  if (local_role == DemoRole::kHost) {
    return suffix.empty() ? "Client" : "Client " + suffix;
  }
  return suffix.empty() ? "Host" : "Host " + suffix;
}

ChatEntry::ptr OpenOrSelectChat(ChatWorkspace& workspace,
                                std::string const& peer_uid,
                                PersistLocalState const& persist) {
  std::string const canonical_uid = TrimAsciiWhitespace(peer_uid);
  if (canonical_uid.empty()) {
    return {};
  }

  for (auto const& entry_ptr : workspace.chats) {
    if (entry_ptr.is_valid() && entry_ptr->peer_uid == canonical_uid) {
      if (workspace.selected_chat_id != entry_ptr.id()) {
        auto sel_event =
            ChatSelectedEvent::ptr::Create(ae::CreateWith{*workspace.domain});
        sel_event->entry_id = entry_ptr.id();
        workspace.Commit(sel_event);
        if (persist) {
          persist();
        }
      }
      return entry_ptr;
    }
  }

  auto new_entry = ChatEntry::ptr::Create(ae::CreateWith{*workspace.domain});
  new_entry->peer_uid = canonical_uid;
  new_entry->display_name =
      ConversationDisplayName(workspace.demo_role, canonical_uid);
  apptraverse::InitializeRuntimeNode(*new_entry);

  // Add through ChatEntryAddedEvent
  auto add_event =
      ChatEntryAddedEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  add_event->entry = new_entry;
  workspace.Commit(add_event);

  // Select through ChatSelectedEvent
  auto sel_event =
      ChatSelectedEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  sel_event->entry_id = new_entry.id();
  workspace.Commit(sel_event);

  if (persist) {
    persist();
  }

  return new_entry;
}

bool BindChat(ChatEntry& entry, apptraverse::Link::ptr link,
              ChatRoom::ptr room, PersistLocalState const& persist) {
  if (!link.is_valid() || !room.is_valid()) {
    return false;
  }
  if (link.domain() != entry.domain || room.domain() != entry.domain) {
    return false;
  }

  // Idempotent no-op check
  if (link->EndpointUid() != entry.peer_uid) {
    return false;
  }

  if (entry.peer_link.is_valid() && entry.room.is_valid() &&
      entry.peer_link.id() == link.id() && entry.room.id() == room.id()) {
    return true;
  }

  // Conflicting nonempty binding returns explicit error
  if (entry.peer_link.is_valid() && entry.peer_link.id() != link.id()) {
    return false;
  }
  if (entry.room.is_valid() && entry.room.id() != room.id()) {
    return false;
  }

  auto event =
      ChatBindingChangedEvent::ptr::Create(ae::CreateWith{*entry.domain});
  event->peer_link = link;
  event->room = room;
  entry.Commit(event);

  if (persist) {
    persist();
  }
  return true;
}

bool BindLocalEndpoint(ChatWorkspace& workspace, std::string const& uid,
                       PersistLocalState const& persist) {
  if (uid.empty()) {
    return false;
  }
  if (workspace.local_endpoint_uid == uid) {
    return true;
  }
  if (!workspace.local_endpoint_uid.empty()) {
    return false;
  }

  auto event =
      LocalEndpointBoundEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  event->endpoint_uid = uid;
  workspace.Commit(event);

  if (persist) {
    persist();
  }
  return true;
}

bool SetDraft(ChatEntry& entry, std::string const& text,
              PersistLocalState const& persist) {
  if (entry.draft == text) {
    return true;
  }

  auto event = DraftChangedEvent::ptr::Create(ae::CreateWith{*entry.domain});
  event->text = text;
  entry.Commit(event);

  if (persist) {
    persist();
  }
  return true;
}

bool SetScroll(ChatEntry& entry, ScrollAnchor const& anchor,
               PersistLocalState const& persist) {
  if (entry.scroll == anchor) {
    return true;
  }

  auto event = ScrollChangedEvent::ptr::Create(ae::CreateWith{*entry.domain});
  event->scroll = anchor;
  entry.Commit(event);

  if (persist) {
    persist();
  }
  return true;
}

bool SetDesktopBounds(ChatWorkspace& workspace, DesktopBounds const& bounds,
                      PersistLocalState const& persist) {
  if (workspace.desktop_bounds == bounds) {
    return true;
  }

  auto event =
      DesktopBoundsChangedEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  event->bounds = bounds;
  workspace.Commit(event);

  if (persist) {
    persist();
  }
  return true;
}

bool SelectChat(ChatWorkspace& workspace, ae::ObjId entry_id,
                PersistLocalState const& persist) {
  if (!entry_id.is_valid()) {
    return false;
  }

  bool found = false;
  for (auto const& entry : workspace.chats) {
    if (entry.is_valid() && entry.id() == entry_id) {
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }

  if (workspace.selected_chat_id == entry_id) {
    return true;
  }

  auto event =
      ChatSelectedEvent::ptr::Create(ae::CreateWith{*workspace.domain});
  event->entry_id = entry_id;
  workspace.Commit(event);

  if (persist) {
    persist();
  }
  return true;
}

SharedEventId SubmitDraft(ChatWorkspace& workspace, ChatEntry& entry,
                          std::uint64_t now_us,
                          PersistLocalState const& persist) {
  if (workspace.local_endpoint_uid.empty() ||
      !entry.room.is_valid() ||
      entry.draft.empty() ||
      now_us == 0 ||
      workspace.next_message_sequence == 0 ||
      workspace.next_message_sequence ==
          std::numeric_limits<std::uint64_t>::max()) {
    return {};
  }

  ChatRoom& room = *entry.room;

  // Determine timestamp for message
  // Adjust supplied time only when necessary to follow the last locally authored message
  std::uint64_t effective_time_us = now_us;
  for (auto it = room.messages.rbegin(); it != room.messages.rend(); ++it) {
    if (it->id.origin_uid == workspace.local_endpoint_uid) {
      if (effective_time_us <= it->timestamp_us) {
        effective_time_us = it->timestamp_us + 1;
      }
      break;
    }
  }

  SharedEventId const identity{
      .origin_uid = workspace.local_endpoint_uid,
      .origin_sequence = workspace.next_message_sequence,
  };

  // 1. Reserve sequence
  auto res_event = MessageSequenceReservedEvent::ptr::Create(
      ae::CreateWith{*workspace.domain});
  res_event->reserved_sequence = identity.origin_sequence;
  workspace.Commit(res_event);

  // Persist reservation BEFORE creating sendable shared Event
  if (persist) {
    persist();
  }

  // 2. Construct MessageValue and shared MessageAddedEvent
  MessageValue msg_val{
      .id = identity,
      .timestamp_us = effective_time_us,
      .text = entry.draft,
  };

  auto msg_event =
      MessageAddedEvent::ptr::Create(ae::CreateWith{*room.domain});
  msg_event->message = msg_val;

  // CommitShared to the room using exactly the same identity and timestamp
  room.CommitShared(msg_event, identity,
                    apptraverse::SharedEventOrder{
                        .timestamp_us = effective_time_us,
                    });

  // Persist room state
  if (persist) {
    persist();
  }

  // 3. Clear draft
  auto clear_draft =
      DraftChangedEvent::ptr::Create(ae::CreateWith{*entry.domain});
  clear_draft->text = "";
  entry.Commit(clear_draft);

  // Persist again
  if (persist) {
    persist();
  }

  return identity;
}

}  // namespace apptraverse::example::chat_demo
