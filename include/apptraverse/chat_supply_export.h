#ifndef APPTRAVERSE_CHAT_SUPPLY_EXPORT_H_
#define APPTRAVERSE_CHAT_SUPPLY_EXPORT_H_
#include <filesystem>
namespace apptraverse::chat {
// Implemented by the Debug producer only; shipping consumers load its header.
bool ExportChatInitialSupply(std::filesystem::path const& output);
}  // namespace apptraverse::chat
#endif
