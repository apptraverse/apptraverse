#ifndef APPTRAVERSE_CHAT_SESSION_H_
#define APPTRAVERSE_CHAT_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "apptraverse/connection_runtime.h"

namespace apptraverse::chat {

enum class Presence { Unknown, Online, Offline };

// Stable presentation key. Headless consumers retain the English diagnostic.
enum class ChatStatus {
  Unspecified,
  Starting,
  Connected,
  WaitingForHost,
  Restored,
  EnterHost,
  RoomCommitFailed,
  RoomCreated,
  InvalidHostId,
  FindingRoom,
  HostCommitFailed,
  MessageTooLong,
  NotMember,
  MessageCommitFailed,
  SnapshotFailed,
  AdmissionFailed,
  OpenProfileFailed,
  NameTooLong,
  CreateProfileFailed,
  Connecting,
  ConnectionFailed,
  IdentitySaveFailed,
  InitializationFailed,
  DeliveryCommitFailed,
  StateCommitFailed,
  Stopped,
  CheckpointFailed,
  Count
};

inline constexpr char const* ChatStatusText(ChatStatus status) {
  constexpr char const* messages[] = {
      "",
      "Starting Aether...",
      "Connected",
      "Waiting for the host...",
      "Session restored",
      "Enter the host Aether ID",
      "Could not commit the room in memory",
      "Room created. Share your Aether ID",
      "Invalid host Aether ID",
      "Finding the host room...",
      "Could not commit the host selection",
      "Message is too long (8192 bytes maximum)",
      "This client is not a room participant",
      "Could not commit the message",
      "Could not commit the returning participant snapshot",
      "Could not commit the participant admission",
      "Could not open the chat profile",
      "Name is too long (256 bytes maximum)",
      "Could not create the chat profile",
      "Connecting...",
      "Transport connection failed",
      "Could not store SDK identity",
      "Could not initialize the chat",
      "Could not commit delivery progress",
      "SharedNode could not commit its state",
      "Stopped",
      "Could not commit chat RAM checkpoint",
  };
  static_assert(sizeof(messages) / sizeof(*messages) ==
                static_cast<std::size_t>(ChatStatus::Count));
  return messages[static_cast<std::size_t>(status)];
}

struct ParticipantView {
  std::string name;
  std::string uid;
  Presence presence{Presence::Unknown};
  bool operator==(ParticipantView const&) const = default;
};

// Value-only publication. The UI owns its copy; no model/SDK pointers cross
// the model thread boundary.
struct ChatMessageView {
  std::string id, author_uid, author_name, text;
  // Original SharedNode event order timestamp, Unix microseconds UTC.
  std::uint64_t sent_at_utc_us{0};
  // True only after every remote member has durably acknowledged the event.
  bool delivered{false};
  // Sender-local elapsed time from commit to all-recipient ACK; absent on replay.
  std::optional<std::uint64_t> ack_elapsed_us;
  bool operator==(ChatMessageView const&) const = default;
};

struct ChatView {
  SessionObservation connection;
  std::string local_uid;
  std::string invite;
  std::string status;
  std::vector<ParticipantView> participants;
  std::vector<std::string> messages;
  std::vector<ChatMessageView> message_records;
  bool push_registered{false};
  int push_notify_status{-1};
  std::uint64_t push_notify_request{};
  bool ready{false};
  bool in_room{false};
  bool locally_online{false};
  bool tcp_path_verified{false};
  bool udp_path_verified{false};
  std::size_t retained_events{0};
  std::size_t pending_packets{0};
  // Progress reports are refreshed periodically even after all data is ACKed.
  std::size_t pending_progress_packets{0};
  ChatStatus status_code{ChatStatus::Unspecified};
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
  // Compatibility hook for platform SDK storage only; chat commits never call it.
  using FlushProfile = std::function<bool()>;
  using AdaptersFactory = std::function<ConnectionAdapters(std::filesystem::path const&)>;
  ChatSession(std::filesystem::path profile, std::string name,
              Changed changed = {});
  ChatSession(std::filesystem::path profile, std::string name, Changed changed,
              PrepareProfile prepare_profile, FlushProfile flush_profile = {},
              AdaptersFactory adapters = {});
  ~ChatSession();
  ChatSession(ChatSession const&) = delete;
  ChatSession& operator=(ChatSession const&) = delete;

  // Configure before Start; token updates may arrive from the Android SDK thread.
  void ConfigurePush(std::string service_uid);
  void RegisterPushToken(std::string token, bool apple = false);
  void RegisterWebPush(std::string endpoint);
  void NotifyPushPeers();
  void Start();
  void Stop();
  // OS connectivity observation, queued onto the SDK/model worker.
  void NetworkChanged(NetworkObservation observation);
  // Ordered model-worker RAM checkpoint; chat/history/receipts never reach disk.
  // Keeps the model, transport and presenters alive during native deactivation.
  void Checkpoint(std::function<void(bool)> completion);
  void CreateRoom();
  // Resolve the selected host's current room over Aether, then join it.
  // Only the host UID is user-visible; room object IDs stay in the protocol.
  void JoinRoom(std::string host_uid);
  void Send(std::string text);
  // Completion runs on the model worker after the local model commit.
  // Rejection after shutdown may complete synchronously on the caller.
  // Every submitted completion is invoked once, including startup failure.
  // A UI may clear its unchanged draft only when committed is true.
  void Send(std::string text, std::function<void(bool)> committed);
  ChatView View() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace apptraverse::chat
#endif
