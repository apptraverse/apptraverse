#ifndef APPTRAVERSE_EXAMPLES_SINGLE_CLIENT_CHAT_REGISTRATION_H_
#define APPTRAVERSE_EXAMPLES_SINGLE_CLIENT_CHAT_REGISTRATION_H_

namespace apptraverse {

// Registers platform-neutral single-client-chat model types. Call once from
// each executable/shared library that uses the example graph.
void EnsureSingleClientChatRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_SINGLE_CLIENT_CHAT_REGISTRATION_H_
