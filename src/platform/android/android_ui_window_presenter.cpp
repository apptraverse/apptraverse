#include "apptraverse/platform/android/android_ui_window_presenter.h"

#include "android_jni_util.h"
#include <algorithm>
#include <cmath>
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/ui/ui_constraint_layout.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/android/android_presentation_host.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::android {

struct AndroidDesktopWindowViewRefs {
  jobject client_mount{nullptr};
};

jobject AndroidPresentationHost::ClientMount(
    AndroidDesktopWindowPresenter const& desktop) {
  return desktop.view_refs_->client_mount;
}

void AndroidPresentationHost::LoadDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  if (content_root == nullptr) {
    WriteFatalStderr(
        "fatal: AndroidDesktopWindowPresenter::OnLoad missing host root\n");
    std::abort();
  }
  JNIEnv* env = RequireUiThreadEnv(*this);
  jclass ui_class = CheckedJni(env, "FindClass", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID create_client = CheckedJni(env, "GetStaticMethodID", [&] { return env->GetStaticMethodID(
      ui_class, "createClientMount", "(Landroid/view/ViewGroup;J)Landroid/widget/FrameLayout;"); });
  presenter.view_refs_ = new AndroidDesktopWindowViewRefs();
  jobject client = CheckedJni(env, "CallStaticObjectMethod", [&] { return env->CallStaticObjectMethod(
      ui_class, create_client, content_root, reinterpret_cast<jlong>(&presenter)); });
  presenter.view_refs_->client_mount = CheckedJni(env, "NewGlobalRef", [&] { return env->NewGlobalRef(client); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(client); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(ui_class); });
  SyncDesktopWindow(presenter);
}

void NotifyAndroidWindowClientSizeFromJava(std::int64_t native_presenter,
                                         std::int32_t width,
                                         std::int32_t height,
                                         std::int32_t density_dpi) {
  auto& presenter = *reinterpret_cast<AndroidDesktopWindowPresenter*>(native_presenter);
  auto& host = *static_cast<AndroidPresentationHost*>(
      static_cast<AndroidUiNativeViews*>(presenter.presentation_host));
  presenter.RefreshLocalLayout(width, height, density_dpi, true);
  auto* local = presenter.LocalDeviceState();
  {
    auto environment = CaptureAndroidDeviceEnvironment(RequireUiThreadEnv(host), host.activity);
    JNIEnv* env = RequireUiThreadEnv(host);
    auto cls = CheckedJni(env, "display class", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
    auto method = CheckedJni(env, "window display method", [&] { return env->GetStaticMethodID(cls,
        "windowDisplayKey", "(Landroid/app/Activity;)Ljava/lang/String;"); });
    auto display_key = static_cast<jstring>(CheckedJni(env, "actual window display", [&] {
      return env->CallStaticObjectMethod(cls, method, host.activity);
    }));
    std::string key = JstringToUtf8(env, display_key);
    env->DeleteLocalRef(display_key); env->DeleteLocalRef(cls);
    // Android reports its client surface in local logical dp, matching layout units.
    UiWindowEnvironment observation{presenter.window.id(), key,
        static_cast<std::uint32_t>(density_dpi), static_cast<std::uint32_t>(density_dpi),
        {0, 0, static_cast<int>(std::lround(width * 160.0 / density_dpi)),
               static_cast<int>(std::lround(height * 160.0 / density_dpi))}};
    presenter.model_proxy->Invoke(local->obj_id, &UiDeviceState::ObserveEnvironment,
        std::move(environment), UiEnvironmentReason::kSystemNotification);
    presenter.model_proxy->Invoke(local->obj_id, &UiDeviceState::ObserveWindow,
        std::move(observation), UiEnvironmentReason::kSystemNotification);
  }

}

void AndroidPresentationHost::SyncDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  JNIEnv* env = RequireUiThreadEnv(*this);
  std::string const title_text = presenter.ResolvePresentedWindowTitle(
      presenter.locale->language_index());
  jclass ui_class = CheckedJni(env, "FindClass", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID set_title = CheckedJni(env, "GetStaticMethodID", [&] { return env->GetStaticMethodID(
      ui_class, "setActivityTitle", "(Landroid/app/Activity;Ljava/lang/String;)V"); });
  jstring jtitle = Utf8ToJstring(env, title_text.c_str());
  CheckedJni(env, "CallStaticVoidMethod", [&] { return env->CallStaticVoidMethod(ui_class, set_title, activity, jtitle); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(jtitle); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(ui_class); });
  if (presenter.client_width_ > 0) presenter.RefreshLocalLayout(presenter.client_width_, presenter.client_height_, presenter.density_dpi_);
}

UiControlLayout AndroidDesktopWindowPresenter::ResolveChildLayout(
    UiControlLayout requested) const {
  auto found = std::find_if(local_rectangles_.begin(), local_rectangles_.end(),
      [&](auto const& rect) { return rect.control_id == requested.control_id; });
  // Initial native mount precedes measurement of the complete control set.
  return found == local_rectangles_.end() ? requested : *found;
}

UiDeviceEnvironment CaptureAndroidDeviceEnvironment(JNIEnv* env, jobject activity) {
  jclass cls = CheckedJni(env, "environment class", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID method = CheckedJni(env, "environment method", [&] { return env->GetStaticMethodID(cls, "deviceEnvironment", "(Landroid/app/Activity;)[Ljava/lang/String;"); });
  auto values = static_cast<jobjectArray>(CheckedJni(env, "environment snapshot", [&] { return env->CallStaticObjectMethod(cls, method, activity); }));
  jsize index = 0;
  auto next = [&]() {
    auto value = static_cast<jstring>(env->GetObjectArrayElement(values, index++));
    auto text = JstringToUtf8(env, value); env->DeleteLocalRef(value); return text;
  };
  auto number = [&]() { return std::stoll(next()); };
  UiDeviceEnvironment result;
  result.supported_settings = kSystemTextScale | kSystemDarkMode | kSystemReducedMotion | kSystemScreenReaderHint;
  result.system_dpi_x = result.system_dpi_y = static_cast<std::uint32_t>(number());
  result.text_scale_milli = static_cast<std::uint32_t>(number());
  result.dark = number() != 0; result.reduced_motion = number() != 0;
  result.screen_reader_hint = number() != 0;
  result.message_font = {"sans-serif", 16, 400, false};
  result.colors.push_back(static_cast<std::uint32_t>(number()));
  result.colors.push_back(static_cast<std::uint32_t>(number()));
  auto count = number();
  for (int i = 0; i < count; ++i) {
    UiMonitorEnvironment monitor; monitor.key = next();
    monitor.bounds = {0, 0, static_cast<int>(number()), static_cast<int>(number())};
    monitor.work_area = {0, 0, static_cast<int>(number()), static_cast<int>(number())};
    monitor.primary = number() != 0; monitor.orientation = static_cast<std::uint32_t>(number());
    monitor.refresh_hz = static_cast<std::uint32_t>(number());
    result.monitors.push_back(std::move(monitor));
  }
  env->DeleteLocalRef(values); env->DeleteLocalRef(cls);
  NormalizeUiDeviceEnvironment(result);
  return result;
}

void AndroidDesktopWindowPresenter::RefreshLocalLayout(int width, int height, int dpi,
                                                       bool require_complete_bindings) {
  client_width_ = width; client_height_ = height; density_dpi_ = dpi;
  if (!view_refs_) return; // Native mount lifecycle; graph links are required.
  auto& host = *static_cast<AndroidPresentationHost*>(static_cast<AndroidUiNativeViews*>(presentation_host));
  JNIEnv* env = RequireUiThreadEnv(host);
  jclass cls = CheckedJni(env, "layout class", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID measure = CheckedJni(env, "measure controls", [&] { return env->GetStaticMethodID(cls, "measureControls", "(Landroid/widget/FrameLayout;)[I"); });
  auto array = static_cast<jintArray>(CheckedJni(env, "native measurements", [&] { return env->CallStaticObjectMethod(cls, measure, view_refs_->client_mount); }));
  std::vector<jint> values(env->GetArrayLength(array));
  env->GetIntArrayRegion(array, 0, values.size(), values.data()); env->DeleteLocalRef(array);
  double const scale = dpi / 160.0;
  std::vector<UiLayoutMeasurement> measurements;
  for (std::size_t i = 0; i < values.size(); i += 5)
    measurements.push_back({static_cast<std::uint32_t>(values[i]), values[i+1] / scale,
        values[i+2] / scale, values[i+3] / scale, values[i+4] / scale});
  auto mounted = [&](auto&& self, UiLayoutNode const& node) -> bool {
    if (node.flow == UiLayoutFlow::kLeaf)
      return std::any_of(measurements.begin(), measurements.end(), [&](auto const& item) { return item.control_id == node.control_id; });
    return std::all_of(node.children.begin(), node.children.end(), [&](auto const& child) { return self(self, child); });
  };
  if (!mounted(mounted, *LayoutRoot())) {
    env->DeleteLocalRef(cls);
    if (require_complete_bindings) {
      WriteFatalStderr("fatal: Android layout requires an unmounted native control\n");
      std::abort();
    }
    // Mirror activation constructs the parent before its children. Java posts
    // the strict check to the next UI dispatch, after synchronous mounting.
    return;
  }
  jmethodID remeasure = CheckedJni(env, "remeasure text", [&] { return env->GetStaticMethodID(cls, "measureControlHeight", "(Landroid/widget/FrameLayout;II)I"); });
  auto result = ComputeUiConstraintLayout(*LayoutRoot(), width / scale, height / scale, measurements,
      [&](std::uint32_t id, double allocated_width) {
        auto measured = *std::find_if(measurements.begin(), measurements.end(), [&](auto const& item) { return item.control_id == id; });
        auto h = CheckedJni(env, "wrapped text measurement", [&] { return env->CallStaticIntMethod(cls, remeasure, view_refs_->client_mount,
            static_cast<jint>(id), static_cast<jint>(std::lround(allocated_width * scale))); });
        measured.min_height = std::max(measured.min_height, h / scale);
        measured.preferred_height = std::max(measured.preferred_height, h / scale); return measured;
      });
  if (!result.valid) { WriteFatalStderr(result.error.c_str()); std::abort(); }
  std::vector<jint> rectangles;
  local_rectangles_.clear();
  for (auto const& rect : result.controls) {
    int x = std::lround(rect.x * scale), y = std::lround(rect.y * scale);
    UiControlLayout native_rect{x, y,
        std::max(1, static_cast<int>(std::lround((rect.x + rect.width) * scale)) - x),
        std::max(1, static_cast<int>(std::lround((rect.y + rect.height) * scale)) - y), rect.control_id};
    local_rectangles_.push_back(native_rect);
    rectangles.insert(rectangles.end(), {static_cast<jint>(rect.control_id), x, y,
        std::max(1, static_cast<int>(std::lround((rect.x + rect.width) * scale)) - x),
        std::max(1, static_cast<int>(std::lround((rect.y + rect.height) * scale)) - y)});
  }
  auto rect_array = env->NewIntArray(rectangles.size());
  env->SetIntArrayRegion(rect_array, 0, rectangles.size(), rectangles.data());
  jmethodID apply = CheckedJni(env, "apply layout", [&] { return env->GetStaticMethodID(cls, "applyControlRects", "(Landroid/widget/FrameLayout;[III)V"); });
  CheckedJni(env, "apply native rectangles", [&] { env->CallStaticVoidMethod(cls, apply, view_refs_->client_mount, rect_array,
      static_cast<jint>(std::ceil(std::max(width / scale, result.content_width) * scale)),
      static_cast<jint>(std::ceil(std::max(height / scale, result.content_height) * scale))); });
  auto* local = LocalDeviceState();
  {
    jmethodID appearance = CheckedJni(env, "appearance method", [&] { return env->GetStaticMethodID(cls, "applyAppearance", "(Landroid/widget/FrameLayout;IZ)V"); });
    CheckedJni(env, "appearance", [&] { env->CallStaticVoidMethod(cls, appearance, view_refs_->client_mount,
        static_cast<jint>(local->appearance.theme), local->appearance.soften_dark ? JNI_TRUE : JNI_FALSE); });
  }
  env->DeleteLocalRef(rect_array); env->DeleteLocalRef(cls);
}

void AndroidPresentationHost::UnloadDesktopWindow(
    AndroidDesktopWindowPresenter& presenter) {
  JNIEnv* env = RequireUiThreadEnv(*this);
  jclass ui_class =
      CheckedJni(env, "FindClass", [&] { return env->FindClass("com/apptraverse/ui/android/AppTraverseAndroidUi"); });
  jmethodID release = CheckedJni(env, "GetStaticMethodID", [&] { return env->GetStaticMethodID(
      ui_class, "releaseClientMount", "(Landroid/widget/FrameLayout;)V"); });
  CheckedJni(env, "CallStaticVoidMethod", [&] { return env->CallStaticVoidMethod(ui_class, release,
                            presenter.view_refs_->client_mount); });
  CheckedJni(env, "DeleteLocalRef", [&] { return env->DeleteLocalRef(ui_class); });
  ClearGlobalRef(*this, presenter.view_refs_->client_mount);
  delete presenter.view_refs_;
  presenter.view_refs_ = nullptr;
  presenter.local_rectangles_.clear();
  presenter.client_width_ = presenter.client_height_ = 0;
}

}  // namespace apptraverse::ui::android

// Existing hosts may register only the original button/list callbacks. Keep
// the new viewport callback resolvable by JNI without changing those hosts.
extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_ui_android_AppTraverseAndroidUi_nativeOnWindowClientSize(
    JNIEnv*, jclass, jlong presenter, jint width, jint height, jint dpi) {
  apptraverse::ui::android::NotifyAndroidWindowClientSizeFromJava(
      presenter, width, height, dpi);
}

extern "C" JNIEXPORT void JNICALL
Java_com_apptraverse_ui_android_AppTraverseAndroidUi_nativeOnAppearanceChanged(
    JNIEnv*, jclass, jlong native_presenter, jint mode, jboolean soften) {
  using namespace apptraverse::ui;
  auto& presenter = *reinterpret_cast<android::AndroidDesktopWindowPresenter*>(native_presenter);
  presenter.model_proxy->Invoke(presenter.LocalDeviceState()->obj_id,
      &UiDeviceState::SetAppearance,
      UiAppearancePreferences{static_cast<UiThemeMode>(mode), soften == JNI_TRUE});
}
