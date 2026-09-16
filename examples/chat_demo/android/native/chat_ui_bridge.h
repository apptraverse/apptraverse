#ifndef APPTRAVERSE_CHAT_DEMO_ANDROID_CHAT_UI_BRIDGE_H_
#define APPTRAVERSE_CHAT_DEMO_ANDROID_CHAT_UI_BRIDGE_H_

#include <jni.h>

#include <cstdint>
#include <string>
#include <vector>

namespace apptraverse::example::chat_demo::android {

class ChatUiBridge {
 public:
  ChatUiBridge() = default;
  ChatUiBridge(JavaVM* vm, jobject global_object, jclass global_class,
               jmethodID on_notify, jmethodID on_state, jmethodID on_stopped);
  ~ChatUiBridge();

  ChatUiBridge(ChatUiBridge const&) = delete;
  ChatUiBridge& operator=(ChatUiBridge const&) = delete;
  ChatUiBridge(ChatUiBridge&& other) noexcept;
  ChatUiBridge& operator=(ChatUiBridge&& other) noexcept;

  bool is_valid() const { return object_ != nullptr; }

  void PostNotify() const;
  void PostState(long selected_entry_id, std::vector<long> const& entry_ids,
                 std::vector<std::string> const& entry_labels,
                 std::string const& draft, std::string const& transcript,
                 std::string const& status, std::string const& presence,
                 bool follow_tail, std::string const& scroll_msg_uid,
                 std::uint64_t scroll_msg_seq, double scroll_offset,
                 bool send_enabled, bool wide_layout) const;
  void PostStopped() const;

 private:
  JNIEnv* AttachedEnv() const;
  void Reset();

  JavaVM* vm_{nullptr};
  jobject object_{nullptr};
  jclass class_{nullptr};
  jmethodID on_notify_{nullptr};
  jmethodID on_state_{nullptr};
  jmethodID on_stopped_{nullptr};
};

ChatUiBridge MakeChatUiBridge(JNIEnv* env, jobject ui_bridge);

}  // namespace apptraverse::example::chat_demo::android

#endif  // APPTRAVERSE_CHAT_DEMO_ANDROID_CHAT_UI_BRIDGE_H_
