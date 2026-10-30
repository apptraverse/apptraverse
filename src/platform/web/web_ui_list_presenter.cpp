#include "apptraverse/platform/web/web_ui_list_presenter.h"

#include <string>
#include <vector>

#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/threading.h>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::web {
namespace {

EM_JS(emscripten::EM_VAL, ApptraverseCreateListElement, (int control_id), {
  var list = document.createElement('div');
  list.className = 'apptraverse-list';
  list.dataset.apptraverseControlId = String(control_id);
  list.setAttribute('role', 'listbox');
  return Emval.toHandle(list);
});

EM_JS(void, ApptraverseSetListLayout, (emscripten::EM_VAL list, int x, int y,
                                       int w, int h), {
  var style = Emval.toValue(list).style;
  style.position = 'absolute';
  style.left = x + 'px';
  style.top = y + 'px';
  style.width = w + 'px';
  style.height = h + 'px';
  style.overflow = 'auto';
});

EM_JS(int, ApptraverseListRowCount, (emscripten::EM_VAL list), {
  return Emval.toValue(list).children.length;
});

EM_JS(void, ApptraverseListClearRows, (emscripten::EM_VAL list), {
  var el = Emval.toValue(list);
  while (el.firstChild) {
    el.removeChild(el.firstChild);
  }
});

EM_JS(void, ApptraverseListAppendRow, (emscripten::EM_VAL list, int row_index,
                                        const char* text_utf8), {
  var row = document.createElement('div');
  row.className = 'apptraverse-list-row';
  row.dataset.rowIndex = String(row_index);
  row.setAttribute('role', 'option');
  row.textContent = UTF8ToString(text_utf8);
  Emval.toValue(list).appendChild(row);
});

EM_JS(void, ApptraverseListUpdateRowText, (emscripten::EM_VAL list, int row_index,
                                            const char* text_utf8), {
  var el = Emval.toValue(list);
  var row = el.children[row_index];
  if (row) {
    row.textContent = UTF8ToString(text_utf8);
  }
});

EM_JS(void, ApptraverseListApplySelection, (emscripten::EM_VAL list,
                                              int selection_index), {
  var el = Emval.toValue(list);
  for (var i = 0; i < el.children.length; ++i) {
    var row = el.children[i];
    if (selection_index >= 0 && i === selection_index) {
      row.classList.add('apptraverse-list-row-selected');
      row.setAttribute('aria-selected', 'true');
    } else {
      row.classList.remove('apptraverse-list-row-selected');
      row.setAttribute('aria-selected', 'false');
    }
  }
});

EM_JS(void, ApptraverseBindListInteraction, (emscripten::EM_VAL list,
                                              void* self), {
  var el = Emval.toValue(list);
  el.addEventListener('click', function(ev) {
    if (ev.detail > 1) {
      return;
    }
    var row = ev.target.closest('.apptraverse-list-row');
    if (!row || !el.contains(row)) {
      return;
    }
    var idx = Number(row.dataset.rowIndex);
    Module['_ApptraverseWebUiListSelectionChanged'](self, idx);
  });
  el.addEventListener('dblclick', function(ev) {
    var row = ev.target.closest('.apptraverse-list-row');
    if (!row || !el.contains(row)) {
      return;
    }
    var idx = Number(row.dataset.rowIndex);
    Module['_ApptraverseWebUiListRowActivated'](self, idx);
  });
});

EM_JS(void, ApptraverseUnbindListInteraction, (emscripten::EM_VAL list), {
  var el = Emval.toValue(list);
  el.replaceWith(el.cloneNode(false));
});

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseWebUiListSelectionChanged(void* ctx,
                                                                int row_index) {
  static_cast<WebListContainerPresenter*>(ctx)->HandleDomSelection(row_index);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWebUiListRowActivated(void* ctx,
                                                            int row_index) {
  static_cast<WebListContainerPresenter*>(ctx)->HandleDomActivation(row_index);
}

}  // extern "C"

struct WebListContainerDomRefs {
  emscripten::EM_VAL list_element{nullptr};
};

void WebListContainerPresenter::HandleDomSelection(int row_index) {
  if (applying_mirror_sync_) {
    return;
  }
  SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(row_index));
}

void WebListContainerPresenter::HandleDomActivation(int row_index) {
  if (applying_mirror_sync_) {
    return;
  }
  SubmitListRowActivatedFromUi(static_cast<std::size_t>(row_index));
}

void WebListContainerPresenter::OnLoad() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebListContainerPresenter::OnLoad off main thread\n");
    std::abort();
  }
  auto* desktop =
      static_cast<WebDesktopWindowPresenter*>(window_presenter.operator->());
  emscripten::EM_VAL const parent = desktop->ClientMountHandle();
  if (parent == nullptr) {
    WriteFatalStderr(
        "fatal: WebListContainerPresenter::OnLoad missing client mount\n");
    std::abort();
  }

  dom_refs_ = new WebListContainerDomRefs();
  dom_refs_->list_element =
      ApptraverseCreateListElement(static_cast<int>(control_id));
  ApptraverseBindListInteraction(dom_refs_->list_element, this);
  AppendDomChild(parent, dom_refs_->list_element);
  SyncFromMirror();
}

void WebListContainerPresenter::SyncFromMirror() {
  int const language_index = window_presenter->locale->language_index();
  std::vector<std::string> desired;
  desired.reserve(container->rows.size());
  for (auto const& row : container->rows) {
    desired.push_back(row->primary_text->Resolve(language_index));
  }

  applying_mirror_sync_ = true;
  int const dom_count = ApptraverseListRowCount(dom_refs_->list_element);
  if (dom_count != static_cast<int>(desired.size())) {
    ApptraverseListClearRows(dom_refs_->list_element);
    for (std::size_t i = 0; i < desired.size(); ++i) {
      ApptraverseListAppendRow(dom_refs_->list_element, static_cast<int>(i),
                               desired[i].c_str());
    }
  } else {
    for (std::size_t i = 0; i < desired.size(); ++i) {
      ApptraverseListUpdateRowText(dom_refs_->list_element, static_cast<int>(i),
                                   desired[i].c_str());
    }
  }
  if (notify_list_selection_change) {
    ApptraverseListApplySelection(dom_refs_->list_element,
                                  container->selection_index);
  }
  applying_mirror_sync_ = false;

  ui::UiControlLayout const layout =
      window_presenter->ResolveChildLayout({layout_x, layout_y, layout_width,
                                            layout_height, control_id});
  ApptraverseSetListLayout(dom_refs_->list_element, layout.x, layout.y,
                           layout.width, layout.height);
}

void WebListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void WebListContainerPresenter::OnUnload() {
  if (dom_refs_ == nullptr) {
    return;
  }
  if (dom_refs_->list_element != nullptr) {
    ApptraverseUnbindListInteraction(dom_refs_->list_element);
    ReleaseOwnedEmvalHandle(dom_refs_->list_element);
    dom_refs_->list_element = nullptr;
  }
  delete dom_refs_;
  dom_refs_ = nullptr;
}

}  // namespace apptraverse::ui::web
