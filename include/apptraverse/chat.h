#ifndef APPTRAVERSE_CHAT_H_
#define APPTRAVERSE_CHAT_H_

#include <cassert>
#include <vector>

#include "aether/obj/obj_ptr.h"

#include "apptraverse/chat_entry.h"
#include "apptraverse/chat_events.h"
#include "apptraverse/node_for.h"
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

  ae::ObjPtr<ChatPresenter> presenter;
  std::vector<ChatEntry::ptr> entries;

  void Apply(JoinClientEvent const& event) {
    assert(domain != nullptr);
    assert(event.client.is_valid());
    auto entry = JoinClientEntry::ptr::Create(ae::CreateWith{*domain});
    entry->client = event.client;
    entries.push_back(entry);
  }

  void Apply(AddMessageEvent const& event) {
    assert(domain != nullptr);
    assert(event.author.is_valid());
    auto entry = MessageEntry::ptr::Create(ae::CreateWith{*domain});
    entry->author = event.author;
    entry->text = event.text;
    entries.push_back(entry);
  }

  Client::ptr FindJoinedClient() const {
    for (auto const& entry : entries) {
      if (!entry.is_valid()) {
        continue;
      }
      auto loaded = entry;
      loaded.Load();
      if (!loaded.is_loaded()) {
        continue;
      }
      if (loaded->GetClassId() != JoinClientEntry::kClassId) {
        continue;
      }
      return static_cast<JoinClientEntry&>(*loaded).client;
    }
    return {};
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_H_
