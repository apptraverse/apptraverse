#ifndef APPTRAVERSE_SURFACES_UI_BRIDGE_H_
#define APPTRAVERSE_SURFACES_UI_BRIDGE_H_

#include <jni.h>

#include <cstdint>
#include <vector>

namespace apptraverse::android {

// Native side of com.apptraverse.surfaces.NativeUiBridge. Holds the only JNI
// global references of this example: the bridge instance and its class.
// Activities and Views are never referenced from native code.
class SurfacesUiBridge {
 public:
  SurfacesUiBridge() = default;
  SurfacesUiBridge(JavaVM* vm, jobject global_object, jclass global_class,
                   jmethodID on_publication, jmethodID on_pages,
                   jmethodID on_stopped);
  ~SurfacesUiBridge();

  SurfacesUiBridge(SurfacesUiBridge const&) = delete;
  SurfacesUiBridge& operator=(SurfacesUiBridge const&) = delete;
  SurfacesUiBridge(SurfacesUiBridge&& other) noexcept;
  SurfacesUiBridge& operator=(SurfacesUiBridge&& other) noexcept;

  bool is_valid() const { return object_ != nullptr; }

  // Called from the model thread: asks the main thread to consume the
  // publication the channel now holds.
  void PostPublication(int kind) const;
  // Called from the main thread after presenters were updated. current_id is
  // the persisted current page, or 0 when the model holds none. is_wide comes
  // from SurfacePresenter::IsWide on the published GUI mirror.
  void PostPages(std::vector<std::int64_t> const& ids,
                 std::vector<std::int32_t> const& numbers,
                 std::int64_t current_id, bool is_wide) const;
  // Called from the model thread after Run returned (state is saved).
  void PostStopped() const;

 private:
  JNIEnv* AttachedEnv() const;
  void Reset();

  JavaVM* vm_{nullptr};
  jobject object_{nullptr};
  jclass class_{nullptr};
  jmethodID on_publication_{nullptr};
  jmethodID on_pages_{nullptr};
  jmethodID on_stopped_{nullptr};
};

// Resolves the bridge methods and creates the global references.
// Returns an invalid bridge when the Java side does not match.
SurfacesUiBridge MakeSurfacesUiBridge(JNIEnv* env, jobject ui_bridge);

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_SURFACES_UI_BRIDGE_H_
