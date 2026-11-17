#include "apptraverse/platform/web/web_js_handle.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"

#include <string>

#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/threading.h>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::web {
namespace {

APPTRAVERSE_EM_JS_HANDLE(ApptraverseCreatePushButtonElement, (int control_id), (control_id), {
  var btn = document.createElement('button');
  btn.type = 'button';
  btn.className = 'apptraverse-push-button';
  btn.dataset.apptraverseControlId = String(control_id);
  return Emval.toHandle(btn);
});

EM_JS(void, ApptraverseSetPushButtonMirror, (emscripten::EM_VAL button,
                                              const char* label_utf8,
                                              int enabled, int visible), {
  var el = Emval.toValue(Number(button));
  el.textContent = UTF8ToString(Number(label_utf8));
  el.disabled = enabled ? 0 : 1;
  el.hidden = !visible;
});

EM_JS(void, ApptraverseSetPushButtonLayout, (emscripten::EM_VAL button, int x,
                                              int y, int w, int h), {
  var style = Emval.toValue(Number(button)).style;
  style.position = 'absolute';
  style.left = x + 'px';
  style.top = y + 'px';
  style.width = w + 'px';
  style.height = h + 'px';
});

EM_JS(void, ApptraverseBindPushButtonClick, (emscripten::EM_VAL button,
                                             void* self), {
  var el = Emval.toValue(Number(button));
  var fn = function(ev) {
    var id=Module['_ApptraverseWebUiPushButtonClicked'](self);
    if(id){var target=el.parentElement.querySelector('[data-apptraverse-control-id="'+id+'"]');if(target)target.focus();}
  };
  el.__apptraversePushClick = fn;
  el.addEventListener('click', fn);
});

EM_JS(void, ApptraverseUnbindPushButtonClick, (emscripten::EM_VAL button), {
  var el = Emval.toValue(Number(button));
  var fn = el.__apptraversePushClick;
  if (fn) {
    el.removeEventListener('click', fn);
    delete el.__apptraversePushClick;
  }
});

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE unsigned ApptraverseWebUiPushButtonClicked(void* ctx) {
  auto& p=*static_cast<WebPushButtonPresenter*>(ctx);p.NotifyClickedFromUi();return p.button->FocusControlAfterClick();
}

}  // extern "C"

struct WebPushButtonDomRefs {
  emscripten::EM_VAL button_element{nullptr};
};

void WebPushButtonPresenter::OnLoad() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebPushButtonPresenter::OnLoad off main thread\n");
    std::abort();
  }
  auto* desktop =
      static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->());
  emscripten::EM_VAL const parent = desktop->ClientMountHandle();
  if (parent == nullptr) {
    WriteFatalStderr(
        "fatal: WebPushButtonPresenter::OnLoad missing client mount\n");
    std::abort();
  }

  dom_refs_ = new WebPushButtonDomRefs();
  dom_refs_->button_element =
      ApptraverseCreatePushButtonElement(static_cast<int>(control_id));
  ApptraverseBindPushButtonClick(dom_refs_->button_element, this);
  AppendDomChild(parent, dom_refs_->button_element);
  SyncFromMirror();
  desktop->RegisterChildControl(dom_refs_->button_element, control_id);
}

void WebPushButtonPresenter::SyncFromMirror() {
  int const language_index = window_presenter->locale->language_index();
  std::string const label = button->label->Resolve(language_index);
  ApptraverseSetPushButtonMirror(dom_refs_->button_element, label.c_str(),
                                 button->enabled() ? 1 : 0, button->visible() ? 1 : 0);
  if(!button->visible())return;
  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  ApptraverseSetPushButtonLayout(dom_refs_->button_element, layout.x, layout.y,
                                 layout.width, layout.height);
}

void WebPushButtonPresenter::OnModelChanged() { SyncFromMirror(); static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->())->RefreshLayout(); }

void WebPushButtonPresenter::OnUnload() {
  if (dom_refs_ == nullptr) {
    return;
  }
  static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->())->UnregisterChildControl(dom_refs_->button_element);

  if (dom_refs_->button_element != nullptr) {
    ApptraverseUnbindPushButtonClick(dom_refs_->button_element);
    RemoveDomNode(dom_refs_->button_element);
    ReleaseOwnedEmvalHandle(dom_refs_->button_element);
    dom_refs_->button_element = nullptr;
  }
  delete dom_refs_;
  dom_refs_ = nullptr;
}

}  // namespace apptraverse::ui::web
