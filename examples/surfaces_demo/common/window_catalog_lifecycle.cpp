#include "window_catalog_lifecycle.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_set>
#include <vector>

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {

namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

}  // namespace

void WindowCatalogModelSession::Run(
    std::function<void(WindowCatalogPublicationKind)> on_published) {
  EnsureObjectRegistration();
  EnsureWindowCatalogGraphRegistration();

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
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
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_catalog = BuildWindowCatalogGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_catalog);
    SaveDistilledRoot(*bootstrap_catalog);
  }
#endif

  {
    DirectoryDomainStorage durable_storage{state_dir};
    ae::Domain domain{durable_storage};
    auto catalog = LoadApplication<WindowCatalog>(domain, CatalogRootId());
    std::filesystem::path const checkpoint_path =
        state_dir / "persistent_model_checkpoint.bin";
    std::error_code checkpoint_error;
    bool const has_checkpoint =
        std::filesystem::exists(checkpoint_path, checkpoint_error);
    if (has_checkpoint) {
      std::ifstream checkpoint_in(checkpoint_path,
                                  std::ios::binary | std::ios::ate);
      auto const checkpoint_size =
          static_cast<std::size_t>(checkpoint_in.tellg());
      checkpoint_in.seekg(0, std::ios::beg);
      std::vector<std::uint8_t> checkpoint_bytes(checkpoint_size);
      if (checkpoint_size > 0) {
        checkpoint_in.read(reinterpret_cast<char*>(checkpoint_bytes.data()),
                           static_cast<std::streamsize>(checkpoint_size));
      }
      ByteSource checkpoint;
      checkpoint.data = checkpoint_bytes.data();
      checkpoint.size = checkpoint_bytes.size();
      checkpoint.pos = 0;
      LoadPersistentModelSnapshot(checkpoint, domain, durable_storage,
                                  *catalog);
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

    runtime.Run(
        domain, *catalog,
        [&](ApplicationPublicationKind kind) {
          on_published(kind == ApplicationPublicationKind::Initial
                           ? WindowCatalogPublicationKind::Initial
                           : WindowCatalogPublicationKind::Incremental);
        },
        [&] {
          pending_focus_for_ui = catalog->pending_focus_window.is_valid()
                                     ? catalog->pending_focus_window.id()
                                     : ae::ObjId{};
          catalog->pending_focus_window = {};
        });

    SaveWindowCatalogPersistentSnapshot(*catalog);
    SavePersistentModelSnapshotToStorage(*catalog, durable_storage);
  }
}

}  // namespace apptraverse
