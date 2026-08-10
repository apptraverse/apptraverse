#ifndef APPTRAVERSE_EXAMPLES_ANDROID_UI_BRIDGE_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_UI_BRIDGE_H_

#include <jni.h>

#include <string>

namespace apptraverse::android {

// Native side of com.apptraverse.singleclientchat.NativeUiBridge.
// Holds the only JNI global references used by this example: the bridge
// instance and its class. Activities are never referenced from native code.
class UiBridge {
 public:
  UiBridge() = default;
  UiBridge(JavaVM* vm, jobject global_object, jclass global_class,
           jmethodID on_status, jmethodID on_transcript,
           jmethodID on_message_committed);
  ~UiBridge();

  UiBridge(UiBridge const&) = delete;
  UiBridge& operator=(UiBridge const&) = delete;
  UiBridge(UiBridge&& other) noexcept;
  UiBridge& operator=(UiBridge&& other) noexcept;

  bool is_valid() const { return object_ != nullptr; }

  void PostStatus(std::string const& status) const;
  void PostTranscript(std::string const& transcript) const;
  void PostMessageCommitted(std::string const& text) const;

 private:
  void CallStringMethod(jmethodID method, std::string const& text) const;
  JNIEnv* AttachedEnv() const;
  void Reset();

  JavaVM* vm_{nullptr};
  jobject object_{nullptr};
  jclass class_{nullptr};
  jmethodID on_status_{nullptr};
  jmethodID on_transcript_{nullptr};
  jmethodID on_message_committed_{nullptr};
};

// Resolves the bridge methods and creates the global references.
// Returns an invalid UiBridge when the Java side does not match.
UiBridge MakeUiBridge(JNIEnv* env, jobject ui_bridge);

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_UI_BRIDGE_H_
