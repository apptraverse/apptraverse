#ifndef APPTRAVERSE_CHAT_DEMO_ANDROID_CHAT_NATIVE_RUNTIME_H_
#define APPTRAVERSE_CHAT_DEMO_ANDROID_CHAT_NATIVE_RUNTIME_H_

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "chat_connectivity.h"
#include "chat_launch_options.h"
#include "chat_model.h"
#include "chat_session.h"
#include "chat_ui_bridge.h"

namespace apptraverse::example::chat_demo::android {

using apptraverse::example::chat_demo::ChatEntry;
using apptraverse::example::chat_demo::ChatSession;
using apptraverse::example::chat_demo::ChatSessionConfig;
using apptraverse::example::chat_demo::ChatUiUpdate;
using apptraverse::example::chat_demo::ChatWorkspace;
using apptraverse::example::chat_demo::LocalConnectivityState;
using apptraverse::example::chat_demo::MessageValue;
using apptraverse::example::chat_demo::OpenPeerRequest;
using apptraverse::example::chat_demo::RoomBootstrapState;
using apptraverse::example::chat_demo::ScrollAnchor;
using apptraverse::example::chat_demo::SessionLifecycleState;

class ChatNativeRuntime {
 public:
  ChatNativeRuntime(std::filesystem::path state_dir, ChatUiBridge ui_bridge);
  ~ChatNativeRuntime();

  void Start();
  void Join();

  void OpenPeer(std::string admin_id, std::optional<std::string> peer_uid);
  void SelectChat(ae::ObjId entry_id);
  void EditDraft(ae::ObjId entry_id, std::string text, std::uint64_t edit_revision);
  void SendDraft(ae::ObjId entry_id, std::string text, std::uint64_t edit_revision);
  void SaveScroll(ae::ObjId entry_id, ScrollAnchor anchor);
  void Checkpoint();
  void RequestStop();
  bool IsStopped() const;
  bool ConsumeUiUpdate();

  void SetWideLayout(bool wide) { wide_layout_ = wide; }

 private:
  struct DraftEditState {
    std::uint64_t local_edit_revision{0};
    std::uint64_t last_published_edit_revision{0};
    std::string local_draft;
  };

  struct EntryViewState {
    DraftEditState draft;
    ScrollAnchor live_scroll{};
    bool live_scroll_valid{false};
  };

  void ConsumeUiUpdatesLocked();
  void PublishStateToJava();
  ChatEntry::ptr FindUiEntry(ae::ObjId entry_id) const;
  EntryViewState& ViewStateFor(ae::ObjId entry_id);
  std::string FormatTranscriptLine(ChatEntry::ptr const& entry,
                                   MessageValue const& msg) const;
  std::string BuildTranscript(ChatEntry::ptr const& entry) const;
  std::string BuildStatusLine() const;
  std::string BuildPresenceLine() const;

  std::filesystem::path state_dir_;
  ChatUiBridge ui_bridge_;
  ChatSession session_;

  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  ChatWorkspace::ptr ui_workspace_;

  std::map<ae::ObjId, EntryViewState> entry_views_;
  std::optional<ae::ObjId> pending_user_selection_;
  ae::ObjId active_entry_id_;

  bool wide_layout_{false};
  std::uint64_t pending_send_revision_{0};
  std::string local_send_error_;

  mutable std::mutex mu_;
  std::atomic<bool> stopped_{false};
};

}  // namespace apptraverse::example::chat_demo::android

#endif  // APPTRAVERSE_CHAT_DEMO_ANDROID_CHAT_NATIVE_RUNTIME_H_
