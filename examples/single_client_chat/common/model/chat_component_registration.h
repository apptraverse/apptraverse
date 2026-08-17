#ifndef APPTRAVERSE_CHAT_COMPONENT_REGISTRATION_H_
#define APPTRAVERSE_CHAT_COMPONENT_REGISTRATION_H_

namespace apptraverse {

// Registers reusable chat-component model types (Client, Chat, events, peers).
// Call once from each executable/shared library that uses the headless chat
// graph. EnsureSingleClientChatRegistration() also calls this.
void EnsureChatComponentRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_CHAT_COMPONENT_REGISTRATION_H_