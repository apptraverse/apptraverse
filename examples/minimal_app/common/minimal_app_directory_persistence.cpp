#include "minimal_app_persistence.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {
int PersistMinimalApplication(MinimalApplication& application,
                              DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  ui::SavePresentedWindow(application.main_window);
  if (application.auxiliary_presented_open) {
    application.auxiliary.window.Load();
    ui::SavePresentedWindow(application.auxiliary.window);
  }
  ae::DomainGraph graph{application.domain};
  MinimalApplication::ptr const root =
      MinimalApplication::ptr::MakeFromThis(&application);
  ae::seri::SeriResult const save_root =
      graph.SaveRoot(root.cached(), MinimalApplicationRootId());
  if (save_root.IsErr()) {
    WriteFatalStderr("fatal: MinimalApplication SaveRoot failed\n");
    return 1;
  }
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: MinimalApplication save failed\n");
    return 1;
  }
  return 0;
}

int PersistMinimalApplicationEmbeddedUserOverlay(
    MinimalApplication& application, DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  int const result = SaveMinimalApplicationEmbeddedUserState(application);
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: MinimalApplication embedded save failed\n");
    return 1;
  }
  return result;
}

int PersistMinimalApplicationEmbeddedUserOverlayFromDomain(
    ae::Domain& domain, DirectoryDomainStorage& user_storage) {
  ae::Ptr<ae::Obj> const held = domain.Find(MinimalApplicationRootId());
  auto& application = static_cast<MinimalApplication&>(*held);
  return PersistMinimalApplicationEmbeddedUserOverlay(application,
                                                      user_storage);
}

}  // namespace apptraverse::example::minimal
