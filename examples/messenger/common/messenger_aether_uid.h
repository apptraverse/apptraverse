#ifndef APPTRAVERSE_MESSENGER_AETHER_UID_H_
#define APPTRAVERSE_MESSENGER_AETHER_UID_H_

#include <string>
#include <string_view>

namespace apptraverse {

// Validate user-entered UID text before any assert-taking Aether string path.
// On success, out is the canonical ae::Format("{}", uid) form.
bool TryCanonicalizeAetherUid(std::string_view raw, std::string& out);

}  // namespace apptraverse

#endif  // APPTRAVERSE_MESSENGER_AETHER_UID_H_
