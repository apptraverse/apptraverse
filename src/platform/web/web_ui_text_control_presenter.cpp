#include "apptraverse/platform/web/web_ui_text_control_presenter.h"
#include <emscripten.h>
#include "apptraverse/platform/web/web_js_handle.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
namespace apptraverse::ui::web {
extern "C" {
EMSCRIPTEN_KEEPALIVE void ApptraverseWebTextEdited(void* p, char const* text) {
  static_cast<WebTextControlPresenter*>(p)->QueueEditedFromUi(text);
}
EMSCRIPTEN_KEEPALIVE void ApptraverseWebTextAccepted(void* p) {
  static_cast<WebTextControlPresenter*>(p)->QueueAcceptFromUi();
}
}
// clang-format off
APPTRAVERSE_EM_JS_HANDLE(
    CreateText, (int kind, int id, void* self), (kind, id, self), {
      var el = document.createElement(
          kind === 0 ? 'span' : kind === 1 ? 'input' : 'textarea');
      el.dataset.apptraverseControlId = String(id);
      el.style.boxSizing = 'border-box';
      el.__edit = () => {
        var p = stringToNewUTF8(el.value);
        Module._ApptraverseWebTextEdited(self, p);
        _free(p);
      };
      el.__accept = e => {
        if (e.key === 'Enter' && kind === 1 && !e.isComposing && !e.repeat) {
          e.preventDefault();
          Module._ApptraverseWebTextAccepted(self);
        }
      };
      if (kind !== 0) {
        el.addEventListener('input', el.__edit);
        el.addEventListener('keydown', el.__accept);
      }
      return Emval.toHandle(el);
    });
EM_JS(void, SyncText,
      (emscripten::EM_VAL handle, char const* str, char const* accessible,
       int enabled, int visible, int readOnly, int apply),
      {
        var el = Emval.toValue(Number(handle)),
            text = UTF8ToString(Number(str)),
            name = UTF8ToString(Number(accessible));
        if (name)
          el.setAttribute('aria-label', name);
        else
          el.removeAttribute('aria-label');
        if (el.tagName === 'SPAN')
          el.textContent = text;
        else if (apply&& el.value !== text) {
          var a = el.selectionStart, z = el.selectionEnd,
              d = el.selectionDirection;
          el.value = text;
          if (document.activeElement === el)
            el.setSelectionRange(Math.min(a, text.length),
                                 Math.min(z, text.length), d);
        }
        el.disabled = !enabled;
        el.hidden = !visible;
        el.readOnly = !!readOnly;
      });
EM_JS(void, LayoutText, (emscripten::EM_VAL handle, int x, int y, int w, int h),
      {
        Object.assign(Emval.toValue(Number(handle)).style, {
          position : 'absolute',
          left : x + 'px',
          top : y + 'px',
          width : w + 'px',
          height : h + 'px'
        });
      });
EM_JS(void, UnbindText, (emscripten::EM_VAL handle), {
  var el = Emval.toValue(Number(handle));
  el.removeEventListener('input', el.__edit);
  el.removeEventListener('keydown', el.__accept);
  delete el.__edit;
  delete el.__accept;
});
// clang-format on
void WebTextControlPresenter::OnLoad() {
  auto* w =
      static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->());
  element_ = CreateText(static_cast<int>(control->kind), control_id, this);
  AppendDomChild(w->ClientMountHandle(), element_);
  Sync();
  w->RegisterChildControl(element_, control_id);
}
void WebTextControlPresenter::Sync() {
  std::string text = control->text;
  for (auto const& row : control->rows) {
    if (!text.empty()) text += '\n';
    text += row;
  }
  SyncText(element_, text.c_str(), control->accessible_label.c_str(),
           control->enabled, control->visible, control->read_only,
           control->revision >= pending_edit);
  if (!control->visible) return;
  auto r = window_presenter->ResolveChildLayout({0, 0, 1, 1, control_id});
  LayoutText(element_, r.x, r.y, r.width, r.height);
}
void WebTextControlPresenter::OnModelChanged() {
  Sync();
  static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->())
      ->RefreshLayout();
}
void WebTextControlPresenter::OnUnload() {
  static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->())
      ->UnregisterChildControl(element_);
  UnbindText(element_);
  RemoveDomNode(element_);
  ReleaseOwnedEmvalHandle(element_);
  element_ = nullptr;
}
}  // namespace apptraverse::ui::web
