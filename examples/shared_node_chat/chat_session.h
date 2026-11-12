#ifndef APPTRAVERSE_EXAMPLES_SHARED_NODE_CHAT_SESSION_H_
#define APPTRAVERSE_EXAMPLES_SHARED_NODE_CHAT_SESSION_H_

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "apptraverse/memory_link.h"

namespace apptraverse::chat {

struct ParticipantView {
  std::string name;
  std::string uid;
  LinkAvailability availability{LinkAvailability::Unknown};
};

// Value-only publication. The UI owns its copy; no model/SDK pointers cross
// the model thread boundary.
struct ChatView {
  std::string local_uid;
  std::string invite;
  std::string status;
  std::vector<ParticipantView> participants;
  std::vector<std::string> messages;
  bool ready{false};
  bool in_room{false};
};

class ChatSession {
 public:
  ChatSession(std::filesystem::path profile, std::string name);
  ~ChatSession();
  ChatSession(ChatSession const&) = delete;
  ChatSession& operator=(ChatSession const&) = delete;

  void Start();
  void Stop();
  void CreateRoom();
  void JoinRoom(std::string invite);
  void Send(std::string text);
  ChatView View() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace apptraverse::chat
#endif
