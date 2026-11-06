#ifndef APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_UI_BRIDGE_H_
#define APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_UI_BRIDGE_H_

#include <jni.h>

#include <cstdint>
#include <vector>

namespace apptraverse::android {

class WindowCatalogUiBridge {
 public:
  WindowCatalogUiBridge() = default;
  WindowCatalogUiBridge(JavaVM* vm, jobject global_object, jclass global_class,
                        jmethodID on_publication, jmethodID on_mobile_pages,
                        jmethodID on_background_save_finished);
  ~WindowCatalogUiBridge();
  WindowCatalogUiBridge(WindowCatalogUiBridge&&) noexcept;
  WindowCatalogUiBridge& operator=(WindowCatalogUiBridge&&) noexcept;

  WindowCatalogUiBridge(WindowCatalogUiBridge const&) = delete;
  WindowCatalogUiBridge& operator=(WindowCatalogUiBridge const&) = delete;

  [[nodiscard]] bool is_valid() const {
    return vm_ != nullptr && object_ != nullptr;
  }

  void PostPublication(int kind) const;
  void PostMobilePages(std::vector<std::int64_t> const& window_ids,
                       std::int64_t active_id) const;
  void PostBackgroundSaveFinished(int persist_result) const;

 private:
  void Reset();
  JNIEnv* AttachedEnv() const;

  JavaVM* vm_{nullptr};
  jobject object_{nullptr};
  jclass class_{nullptr};
  jmethodID on_publication_{nullptr};
  jmethodID on_mobile_pages_{nullptr};
  jmethodID on_background_save_finished_{nullptr};
};

WindowCatalogUiBridge MakeWindowCatalogUiBridge(JNIEnv* env, jobject ui_bridge);

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_EXAMPLE_SURFACES_WINDOW_CATALOG_UI_BRIDGE_H_
