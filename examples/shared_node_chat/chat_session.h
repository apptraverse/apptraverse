#ifndef APPTRAVERSE_EXAMPLES_SHARED_NODE_CHAT_SESSION_H_
#define APPTRAVERSE_EXAMPLES_SHARED_NODE_CHAT_SESSION_H_

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace apptraverse::chat {

struct ParticipantView {
  std::string name;
  std::string uid;
  bool operator==(ParticipantView const&) const = default;
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
  bool operator==(ChatView const&) const = default;
};

class ChatSession {
 public:
  // Called after a changed value publication, outside its lock, on the model
  // thread. The consumer schedules UI work; it must outlive Stop().
  using Changed = std::function<void()>;
  ChatSession(std::filesystem::path profile, std::string name, Changed changed = {});
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
