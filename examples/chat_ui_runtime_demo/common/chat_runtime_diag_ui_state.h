#ifndef CHAT_RUNTIME_DIAG_UI_STATE_H_
#define CHAT_RUNTIME_DIAG_UI_STATE_H_

#include <cstddef>

namespace chat {

struct ChatRuntimeDiagUiState {
  std::size_t room_journal_count{0};
  std::size_t client_journal_count{0};
};

}  // namespace chat

#endif  // CHAT_RUNTIME_DIAG_UI_STATE_H_
