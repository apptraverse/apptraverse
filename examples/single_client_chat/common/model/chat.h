#ifndef APPTRAVERSE_CHAT_H_
#define APPTRAVERSE_CHAT_H_

#include <cassert>
#include <vector>

#include "model/chat_entry.h"
#include "model/chat_events.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class ChatPresenter;

class Chat : public NodeFor<Chat> {
  APPTRAVERSE_OBJECT(Chat, Node, 0)

 protected:
  Chat() = default;

 public:
  explicit Chat(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter), AE_MMBR(entries))

  LocalPtr<ChatPresenter> presenter;
  std::vector<ChatEntry> entries;

  void Apply(JoinClientEvent const& event) {
    assert(event.client.is_valid());
    ChatEntry entry{};
    entry.kind = ChatEntryKind::kJoined;
    entry.client = event.client;
    entries.push_back(std::move(entry));
  }

  void Apply(AddMessageEvent const& event) {
    assert(event.author.is_valid());
    ChatEntry entry{};
    entry.kind = ChatEntryKind::kMessage;
    entry.client = event.author;
    entry.text = event.text;
    entries.push_back(std::move(entry));
  }

  bool CanApply(AddMessageEvent const& event) const {
    assert(event.author.is_valid());
    auto const author_id = event.author.id();
    for (auto const& entry : entries) {
      if (entry.kind == ChatEntryKind::kJoined && entry.client.is_valid() &&
          entry.client.id() == author_id) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_H_
