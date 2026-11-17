#include "apptraverse/platform/web/web_js_handle.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "apptraverse/noninteractive_crt.h"
#include <algorithm>
#include <cmath>
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/ui/ui_desktop_window.h"

#include <emscripten/em_js.h>
#include <emscripten/threading.h>

namespace apptraverse::ui::web {
namespace {
bool Media(char const* query) {
  return emscripten::val::global("window").call<emscripten::val>("matchMedia", std::string(query))["matches"].as<bool>();
}
}
UiDeviceEnvironment ReadWebDeviceEnvironment() {
  using emscripten::val;
  UiDeviceEnvironment result;
  auto browser = val::global("window"), screen = browser["screen"];
  auto const ratio = browser["devicePixelRatio"].as<double>();
  result.system_dpi_x = result.system_dpi_y = static_cast<unsigned>(std::lround(96 * ratio));
  result.supported_settings = kSystemDarkMode | kSystemHighContrast | kSystemReducedMotion;
  result.dark = Media("(prefers-color-scheme: dark)");
  result.high_contrast = Media("(forced-colors: active)");
  result.reduced_motion = Media("(prefers-reduced-motion: reduce)");
  auto style = browser.call<val>("getComputedStyle", val::global("document")["body"]);
  result.message_font.family = style["fontFamily"].as<std::string>();
  result.message_font.height = static_cast<int>(std::lround(std::stod(style["fontSize"].as<std::string>())));
  auto const font_weight = style["fontWeight"].as<std::string>();
  result.message_font.weight = font_weight == "normal" ? 400 : font_weight == "bold" ? 700 : std::stoi(font_weight);
  result.message_font.italic = style["fontStyle"].as<std::string>() != "normal";
  auto pixel = [ratio](int n) { return static_cast<int>(std::lround(n * ratio)); };
  UiDisplayRect bounds{0, 0, pixel(screen["width"].as<int>()), pixel(screen["height"].as<int>())};
  UiDisplayRect work{pixel(screen["availLeft"].isUndefined() ? 0 : screen["availLeft"].as<int>()),
      pixel(screen["availTop"].isUndefined() ? 0 : screen["availTop"].as<int>()),
      pixel(screen["availWidth"].as<int>()), pixel(screen["availHeight"].as<int>())};
  // The ordinary Screen API exposes only the current browser screen. No false
  // claim of multi-monitor enumeration or permission-dependent screen identity.
  result.monitors.push_back({"browser-current-screen", bounds, work, true, bounds.width >= bounds.height ? 0u : 90u, 0});
  return result;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void ApptraverseWebWindowRefresh(void* context, int environment) {
  static_cast<WebDesktopWindowPresenter*>(context)->RefreshLocalPresentation(environment != 0);
}
EMSCRIPTEN_KEEPALIVE void ApptraverseWebWindowFinishLayout(void* context) {
  static_cast<WebDesktopWindowPresenter*>(context)->RefreshLayout(true);
}
}

EM_JS(void, ApptraverseObserveDesktopEnvironment, (emscripten::EM_VAL handle, void* self), {
  var el = Emval.toValue(Number(handle));
  var refresh = function() { Module._ApptraverseWebWindowRefresh(self, 1); };
  var resize = new ResizeObserver(function() { Module._ApptraverseWebWindowRefresh(self, 0); });
  resize.observe(el);
  var media = ['(prefers-color-scheme: dark)', '(forced-colors: active)', '(prefers-reduced-motion: reduce)'].map(function(q) { return matchMedia(q); });
  media.forEach(function(m) { m.addEventListener('change', refresh); });
  window.addEventListener('resize', refresh);
  if (window.visualViewport) window.visualViewport.addEventListener('resize', refresh);
  el.__apptraverseEnvironment = { resize: resize, media: media, refresh: refresh, pending: 0 };
});
EM_JS(void, ApptraverseStopDesktopEnvironment, (emscripten::EM_VAL handle), {
  var el = Emval.toValue(Number(handle)), e = el.__apptraverseEnvironment;
  if (!e) return;
  e.resize.disconnect(); e.media.forEach(function(m) { m.removeEventListener('change', e.refresh); });
  window.removeEventListener('resize', e.refresh);
  if (window.visualViewport) window.visualViewport.removeEventListener('resize', e.refresh);
  if (e.pending) cancelAnimationFrame(e.pending);
  delete el.__apptraverseEnvironment;
});
EM_JS(void, ApptraverseScheduleDesktopLayout, (emscripten::EM_VAL handle, void* self), {
  var el = Emval.toValue(Number(handle)), e = el.__apptraverseEnvironment;
  if (!e || e.pending) return;
  e.pending = requestAnimationFrame(function() { e.pending = 0; Module._ApptraverseWebWindowFinishLayout(self); });
});
EM_JS(void, ApptraverseApplyDesktopAppearance, (emscripten::EM_VAL handle, int mode, int soften, int high_contrast), {
  var el = Emval.toValue(Number(handle));
  var dark = mode === 2 || (mode === 0 && matchMedia('(prefers-color-scheme: dark)').matches);
  el.style.colorScheme = high_contrast ? 'light dark' : dark ? 'dark' : 'light';
  el.dataset.apptraverseTheme = high_contrast ? 'system' : dark ? (soften ? 'soft-dark' : 'dark') : 'light';
  el.style.setProperty('--at-background', dark && soften && !high_contrast ? '#29323c' : 'Canvas');
  el.style.setProperty('--at-text', dark && soften && !high_contrast ? '#e8edf2' : 'CanvasText');
});
APPTRAVERSE_EM_JS_HANDLE(ApptraverseMeasureDesktopControl,
    (emscripten::EM_VAL handle, double available), (handle, available), {
  var original = Emval.toValue(Number(handle));
  var style = getComputedStyle(original), clone = original.cloneNode(false);
  clone.textContent = original.tagName === 'BUTTON' ? original.textContent : 'Mg';
  clone.removeAttribute('id');
  clone.style.cssText = original.style.cssText;
  clone.style.position = 'fixed'; clone.style.visibility = 'hidden';
  clone.style.left = '-100000px'; clone.style.top = '0';
  clone.style.height = 'auto'; clone.style.minHeight = '0'; clone.style.maxHeight = 'none';
  clone.style.whiteSpace = 'normal'; clone.style.minWidth = '0'; clone.style.maxWidth = 'none';
  clone.style.font = style.font; clone.style.padding = style.padding; clone.style.border = style.border;
  original.parentElement.appendChild(clone);
  clone.style.width = 'min-content'; var min = clone.getBoundingClientRect();
  var minWidth = min.width, minHeight = min.height;
  clone.style.width = available > 0 ? available + 'px' : 'max-content';
  var pref = clone.getBoundingClientRect();
  var result = original.tagName === 'BUTTON' ? [minWidth, Math.max(minHeight, pref.height), pref.width, pref.height] : [0, minHeight, 0, 0];
  clone.remove(); return Emval.toHandle(result);
});
EM_JS(void, ApptraverseArrangeDesktopControl, (emscripten::EM_VAL handle, double x, double y, double width, double height), {
  var s = Emval.toValue(Number(handle)).style;
  s.position = 'absolute'; s.left = x + 'px'; s.top = y + 'px';
  s.width = width + 'px'; s.height = height + 'px';
});


APPTRAVERSE_EM_JS_HANDLE(ApptraverseCreateDesktopWindowElement, (), (), {
  var windowEl = document.createElement('div');
  windowEl.className = 'apptraverse-desktop-window';
  windowEl.dataset.apptraverseDesktopWindow = '1';
  var clientEl = document.createElement('div');
  clientEl.className = 'apptraverse-desktop-client';
  windowEl.appendChild(clientEl);
  return Emval.toHandle(windowEl);
});

APPTRAVERSE_EM_JS_HANDLE(ApptraverseDesktopClientElement, (emscripten::EM_VAL window_el), (window_el), {
  return Emval.toHandle(Emval.toValue(Number(window_el)).firstChild);
});

EM_JS(void, ApptraverseMountDesktopWindow, (emscripten::EM_VAL mount,
                                            emscripten::EM_VAL window_el), {
  Emval.toValue(Number(mount)).appendChild(Emval.toValue(Number(window_el)));
});

EM_JS(void, ApptraverseSetDesktopWindowTitleDataset,
      (emscripten::EM_VAL window_el, const char* title_utf8), {
  Emval.toValue(Number(window_el)).dataset.apptraverseWindowTitle =
      UTF8ToString(Number(title_utf8));
});

EM_JS(void, ApptraverseSetDesktopWindowGeometry, (emscripten::EM_VAL window_el,
                                                  int x, int y, int w, int h), {
  var el = Emval.toValue(Number(window_el));
  var style = el.style;
  style.left = x + 'px';
  style.top = y + 'px';
  style.width = w + 'px';
  style.height = h + 'px';
  var client = el.firstChild;
  client.style.width = w + 'px';
  client.style.height = h + 'px';
});

struct WebDesktopWindowDomRefs {
  emscripten::EM_VAL window_element{nullptr};
  emscripten::EM_VAL client_element{nullptr};
};

static void ReleaseDomRefs(WebDesktopWindowDomRefs& refs) {
  RemoveDomNode(refs.window_element);
  ReleaseOwnedEmvalHandle(refs.client_element);
  refs.client_element = nullptr;
  ReleaseOwnedEmvalHandle(refs.window_element);
  refs.window_element = nullptr;
}

void WebDesktopWindowPresenter::OnLoad() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebDesktopWindowPresenter::OnLoad off main thread\n");
    std::abort();
  }
  auto* host = static_cast<WebPresentationHost*>(presentation_host);
  if (host == nullptr || host->mount == nullptr) {
    WriteFatalStderr(
        "fatal: WebDesktopWindowPresenter::OnLoad missing presentation host "
        "mount\n");
    std::abort();
  }

  dom_refs_ = new WebDesktopWindowDomRefs();
  dom_refs_->window_element = ApptraverseCreateDesktopWindowElement();
  dom_refs_->client_element =
      ApptraverseDesktopClientElement(dom_refs_->window_element);
  ApptraverseMountDesktopWindow(host->mount, dom_refs_->window_element);
  SyncGeometryFromMirror();
  SyncTitleFromMirror();
  ApptraverseObserveDesktopEnvironment(dom_refs_->window_element, this);
  RefreshLocalPresentation();
}

void WebDesktopWindowPresenter::SyncTitleFromMirror() {
  std::string const title_text =
      ResolvePresentedWindowTitle(locale->language_index());
  ApptraverseSetDesktopWindowTitleDataset(dom_refs_->window_element,
                                          title_text.c_str());
}

void WebDesktopWindowPresenter::SyncGeometryFromMirror() {
  auto const desktop = desktop_window();
  ApptraverseSetDesktopWindowGeometry(dom_refs_->window_element, desktop->x(),
                                      desktop->y(), desktop->client_width(),
                                      desktop->client_height());
}

void WebDesktopWindowPresenter::OnModelChanged() {
  SyncGeometryFromMirror();
  SyncTitleFromMirror();
  RefreshLocalPresentation();
}

void WebDesktopWindowPresenter::OnUnload() {
  ApptraverseStopDesktopEnvironment(dom_refs_->window_element);
  native_children_.clear();
  ReleaseDomRefs(*dom_refs_);
  delete dom_refs_;
  dom_refs_ = nullptr;
}

void WebDesktopWindowPresenter::RegisterChildControl(emscripten::EM_VAL child, std::uint32_t id) {
  native_children_.push_back({child, id}); RefreshLayout();
}
void WebDesktopWindowPresenter::UnregisterChildControl(emscripten::EM_VAL child) {
  std::erase_if(native_children_, [child](auto const& entry) { return entry.element == child; });
}
bool WebDesktopWindowPresenter::NativeMeasurementsReady() const {
  auto* root = LayoutRoot(); if (!root) return true;
  auto ready = [&](auto&& self, UiLayoutNode const& node) -> bool {
    if (node.flow == UiLayoutFlow::kLeaf) return std::any_of(native_children_.begin(), native_children_.end(), [&](auto const& c) { return c.control_id == node.control_id; });
    return std::all_of(node.children.begin(), node.children.end(), [&](auto const& c) { return self(self, c); });
  };
  return ready(ready, *root);
}
UiConstraintLayoutResult WebDesktopWindowPresenter::ComputeLocalLayout() const {
  std::vector<UiLayoutMeasurement> measurements;
  auto measure = [](NativeChild const& child, double width) {
    auto value = emscripten::val::take_ownership(ApptraverseMeasureDesktopControl(child.element, width));
    return UiLayoutMeasurement{child.control_id, value[0].as<double>(), value[1].as<double>(), value[2].as<double>(), value[3].as<double>()};
  };
  for (auto const& child : native_children_) measurements.push_back(measure(child, 0));
  auto remeasure = [&](std::uint32_t id, double width) {
    auto found = std::find_if(native_children_.begin(), native_children_.end(), [id](auto const& c) { return c.control_id == id; });
    return measure(*found, width);
  };
  // Borrowed DOM handle remains owned by dom_refs_.
  auto win = emscripten::val::take_ownership(ApptraverseDesktopClientElement(dom_refs_->window_element));
  auto parent = win["parentElement"];
  auto result = ComputeUiConstraintLayout(*LayoutRoot(), parent["clientWidth"].as<double>(), parent["clientHeight"].as<double>(), measurements, remeasure);
  if (!result.valid) { WriteFatalStderr(result.error.c_str()); std::abort(); }
  return result;
}
UiControlLayout WebDesktopWindowPresenter::ResolveChildLayout(UiControlLayout requested) const {
  if (!LayoutRoot() || !NativeMeasurementsReady()) return requested;
  auto result = ComputeLocalLayout();
  auto found = std::find_if(result.controls.begin(), result.controls.end(), [&](auto const& r) { return r.control_id == requested.control_id; });
  if (found == result.controls.end()) { WriteFatalStderr("fatal: DOM control missing from required layout\n"); std::abort(); }
  return {static_cast<int>(std::lround(found->x)), static_cast<int>(std::lround(found->y)),
      std::max(1, static_cast<int>(std::lround(found->width))), std::max(1, static_cast<int>(std::lround(found->height))), requested.control_id};
}
void WebDesktopWindowPresenter::RefreshLayout(bool require_complete) {
  if (!dom_refs_ || applying_layout_ || !LayoutRoot()) return;
  if (!NativeMeasurementsReady()) {
    if (require_complete) { WriteFatalStderr("fatal: DOM required layout control was not presented\n"); std::abort(); }
    ApptraverseScheduleDesktopLayout(dom_refs_->window_element, this); return;
  }
  applying_layout_ = true;
  auto result = ComputeLocalLayout();
  for (auto const& rect : result.controls) {
    auto child = std::find_if(native_children_.begin(), native_children_.end(), [&](auto const& c) { return c.control_id == rect.control_id; });
    ApptraverseArrangeDesktopControl(child->element, rect.x, rect.y, rect.width, rect.height);
  }
  auto client = emscripten::val::take_ownership(ApptraverseDesktopClientElement(dom_refs_->window_element));
  client["style"].set("minWidth", std::to_string(result.content_width) + "px");
  client["style"].set("minHeight", std::to_string(result.content_height) + "px");
  applying_layout_ = false;
}
void WebDesktopWindowPresenter::RefreshLocalPresentation(bool environment_changed) {
  if (!dom_refs_) return;
  auto* device = LocalDeviceState();
  auto environment = ReadWebDeviceEnvironment();
  if (device) {
    ApptraverseApplyDesktopAppearance(dom_refs_->window_element, static_cast<int>(device->appearance.theme), device->appearance.soften_dark, environment.high_contrast);
    auto* host = static_cast<WebPresentationHost*>(presentation_host);
    bool const accepts_observations = host->accepts_runtime_observations;
    if (environment_changed && accepts_observations) model_proxy->Invoke(device->obj_id, &UiDeviceState::ObserveEnvironment, environment, UiEnvironmentReason::kSystemNotification);
    auto client = emscripten::val::take_ownership(ApptraverseDesktopClientElement(dom_refs_->window_element));
    auto parent = client["parentElement"];
    UiWindowEnvironment observation{window.id(), "browser-current-screen", environment.system_dpi_x, environment.system_dpi_y,
      {0, 0, parent["clientWidth"].as<int>(), parent["clientHeight"].as<int>()}};
    if (accepts_observations && observation.frame_dip.width > 0 && observation.frame_dip.height > 0)
      model_proxy->Invoke(device->obj_id, &UiDeviceState::ObserveWindow, observation, UiEnvironmentReason::kSystemNotification);
  }
  RefreshLayout();
}

void* WebDesktopWindowPresenter::NativeChildParentHandle() const {
  return reinterpret_cast<void*>(dom_refs_->client_element);
}

emscripten::EM_VAL WebDesktopWindowPresenter::ClientMountHandle() const {
  return dom_refs_->client_element;
}

}  // namespace apptraverse::ui::web
