#ifndef APPTRAVERSE_CHAT_SESSION_H_
#define APPTRAVERSE_CHAT_SESSION_H_

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace apptraverse::chat {

enum class Presence { Unknown, Online, Offline };

struct ParticipantView {
  std::string name;
  std::string uid;
  Presence presence{Presence::Unknown};
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
  bool locally_online{false};
  bool tcp_path_verified{false};
  bool udp_path_verified{false};
  std::size_t retained_events{0};
  std::size_t pending_packets{0};
  bool operator==(ChatView const&) const = default;
};

class ChatSession {
 public:
  // Called after a changed value publication, outside its lock, on the model
  // thread. The consumer schedules UI work; it must outlive Stop().
  using Changed = std::function<void()>;
  // Runtime platform setup runs on the model worker before opening its profile.
  // Browser OPFS must be mounted there, without blocking the browser UI thread.
  using PrepareProfile = std::function<bool()>;
  ChatSession(std::filesystem::path profile, std::string name,
              Changed changed = {});
  ChatSession(std::filesystem::path profile, std::string name, Changed changed,
              PrepareProfile prepare_profile);
  ~ChatSession();
  ChatSession(ChatSession const&) = delete;
  ChatSession& operator=(ChatSession const&) = delete;

  void Start();
  void Stop();
  // Ordered model-worker checkpoint; completion runs there after USER is durable.
  // Keeps the model, transport and presenters alive during native deactivation.
  void Checkpoint(std::function<void(bool)> completion);
  void CreateRoom();
  // Resolve the selected host's current room over Aether, then join it.
  // Only the host UID is user-visible; room object IDs stay in the protocol.
  void JoinRoom(std::string host_uid);
  void Send(std::string text);
  // Completion runs on the model worker after the local model commit.
  // A UI may clear its unchanged draft only when committed is true.
  void Send(std::string text, std::function<void(bool)> committed);
  ChatView View() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace apptraverse::chat
#endif
