#ifndef APPTRAVERSE_CHAT_ENTRY_H_
#define APPTRAVERSE_CHAT_ENTRY_H_

#include <string>

#include "aether/obj/obj.h"

#include "apptraverse/client.h"

namespace apptraverse {

class ChatEntry : public ae::Obj {
  AE_OBJECT(ChatEntry, Obj, 0)

 protected:
  ChatEntry() = default;

 public:
  explicit ChatEntry(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()
};

class JoinClientEntry : public ChatEntry {
  AE_OBJECT(JoinClientEntry, ChatEntry, 0)

 protected:
  JoinClientEntry() = default;

 public:
  explicit JoinClientEntry(ae::ObjProp prop) : ChatEntry{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client))

  Client::ptr client;
};

class MessageEntry : public ChatEntry {
  AE_OBJECT(MessageEntry, ChatEntry, 0)

 protected:
  MessageEntry() = default;

 public:
  explicit MessageEntry(ae::ObjProp prop) : ChatEntry{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(author), AE_MMBR(text))

  Client::ptr author;
  std::string text;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_ENTRY_H_
