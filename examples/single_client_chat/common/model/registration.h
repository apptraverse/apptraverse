#ifndef APPTRAVERSE_EXAMPLES_SINGLE_CLIENT_CHAT_REGISTRATION_H_
#define APPTRAVERSE_EXAMPLES_SINGLE_CLIENT_CHAT_REGISTRATION_H_

namespace apptraverse {

// Registers demo shell model types (App/Window/presenters) and ensures the
// reusable chat-component types are registered first.
void EnsureSingleClientChatRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_SINGLE_CLIENT_CHAT_REGISTRATION_H_