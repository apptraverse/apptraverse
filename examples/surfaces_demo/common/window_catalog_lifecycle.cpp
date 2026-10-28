#include "window_catalog_lifecycle.h"

#include <filesystem>
#include <optional>
#include <unordered_set>
#include <vector>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/distilled_domain_export.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
#include "window_catalog_distilled.h"
#endif
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

int WindowCatalogModelSession::Run(
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

#if defined(APPTRAVERSE_ENABLE_DISTILLATION) ||                           \
    defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile\n");
    std::abort();
  }
  bool const profile_absent = !profile_exists ||
      std::filesystem::is_empty(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile contents\n");
    std::abort();
  }
  if (profile_absent) {
#if defined(APPTRAVERSE_WINDOW_CATALOG_USE_EMBEDDED_DISTILL)
    if (!WriteEmbeddedDistilledGraphToDirectory(
            state_dir,
            embedded_distilled::window_catalog::kEmbeddedGraph)) {
      WriteFatalStderr(
          "fatal: embedded window catalog bootstrap write failed\n");
      std::abort();
    }
#else
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_catalog = BuildWindowCatalogGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_catalog);
    SaveDistilledRoot(*bootstrap_catalog);
#endif
  }
#endif

  {
    DirectoryDomainStorage durable_storage{state_dir};
    ae::Domain domain{durable_storage};
    auto catalog = LoadApplication<WindowCatalog>(domain, CatalogRootId());
    ui::LoadPresentedWindow(catalog->main_window);
    catalog->list.Load();
    if (!catalog->list) {
      WriteFatalStderr("fatal: WindowCatalog required list missing\n");
      std::abort();
    }
    catalog->locale.Load();
    if (!catalog->locale) {
      WriteFatalStderr("fatal: WindowCatalog required locale missing\n");
      std::abort();
    }
    auto& chrome_presenters = catalog->presenters;
    chrome_presenters.list_presenter.Load();
    chrome_presenters.add_button_presenter.Load();
    chrome_presenters.open_button_presenter.Load();
    chrome_presenters.delete_button_presenter.Load();
    chrome_presenters.language_button_presenter.Load();
    if (!chrome_presenters.list_presenter ||
        !chrome_presenters.add_button_presenter ||
        !chrome_presenters.open_button_presenter ||
        !chrome_presenters.delete_button_presenter ||
        !chrome_presenters.language_button_presenter) {
      WriteFatalStderr(
          "fatal: WindowCatalog required chrome presenter missing\n");
      std::abort();
    }
    auto& chrome_buttons = catalog->buttons;
    chrome_buttons.add_button.Load();
    chrome_buttons.open_button.Load();
    chrome_buttons.delete_button.Load();
    chrome_buttons.language_button.Load();
    if (!chrome_buttons.add_button || !chrome_buttons.open_button ||
        !chrome_buttons.delete_button || !chrome_buttons.language_button) {
      WriteFatalStderr("fatal: WindowCatalog required chrome button missing\n");
      std::abort();
    }
    auto& localized_texts = catalog->texts;
    localized_texts.label_add.Load();
    localized_texts.label_open.Load();
    localized_texts.label_delete.Load();
    localized_texts.label_language.Load();
    localized_texts.window_title_prefix.Load();
    if (!localized_texts.label_add || !localized_texts.label_open ||
        !localized_texts.label_delete || !localized_texts.label_language ||
        !localized_texts.window_title_prefix) {
      WriteFatalStderr(
          "fatal: WindowCatalog required localized text missing\n");
      std::abort();
    }
    int const language = catalog->locale->language_index();
    catalog->MaterializeCatalogStrings(language);
    for (auto const& row : catalog->list->rows) {
      auto* holder = static_cast<WindowHolder*>(&*row);
      if (!holder->primary_text.is_loaded()) {
        holder->primary_text.Load();
      }
      catalog->MaterializeHolderListText(*holder, language);
    }
    catalog->RefreshChromeButtonEnabledState();
    RestoreLoadedCatalogWindows(*catalog);

    runtime.Run(domain, *catalog, std::move(on_published), [&] {
          pending_focus_for_ui = catalog->pending_focus_window.is_valid()
                                     ? catalog->pending_focus_window.id()
                                     : ae::ObjId{};
          catalog->pending_focus_window = {};
        });

    durable_storage.BeginSaveSession();
    SaveWindowCatalogPersistentSnapshot(*catalog);
    if (durable_storage.SaveSessionHadFailure()) {
      WriteFatalStderr("fatal: WindowCatalog shutdown save failed\n");
      return 1;
    }
  }
  return 0;
}

}  // namespace apptraverse
