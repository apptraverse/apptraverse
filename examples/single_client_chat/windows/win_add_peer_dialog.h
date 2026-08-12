#ifndef APPTRAVERSE_EXAMPLES_WIN_ADD_PEER_DIALOG_H_
#define APPTRAVERSE_EXAMPLES_WIN_ADD_PEER_DIALOG_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <functional>
#include <string>

namespace apptraverse {

enum class AddPeerUiResult {
  Ok,
  Invalid,
  Self,
};

// Modal "Add participant" dialog. local_aether_uid is shown read-only/selectable.
// on_add receives the trimmed remote UID text and returns validation outcome.
// Returns true when Add succeeded (dialog closed with Ok).
bool ShowAddPeerDialog(
    HWND owner, std::string const& local_aether_uid,
    std::function<AddPeerUiResult(std::string const&)> on_add);

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_ADD_PEER_DIALOG_H_
