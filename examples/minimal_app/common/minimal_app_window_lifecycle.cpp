#include "minimal_app_window_lifecycle.h"

#include "minimal_application.h"

#include "apptraverse/application_model_session_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

#include <set>

namespace apptraverse::example::minimal {

void MinimalApplication::BindEmbeddedUserSession(
    ApplicationModelSessionStorage* session) {
  embedded_user_session_ = session;
}

ApplicationModelSessionStorage* MinimalApplication::EmbeddedUserSession() const {
  return embedded_user_session_;
}

void SealAuxiliaryWindowClosed(MinimalApplication& application) {
  application.auxiliary_presented_open = false;
  if (application.auxiliary.window.is_valid() &&
      application.auxiliary.window.is_loaded()) {
    ae::ObjId const aux_window_id = application.auxiliary.window.id();
    std::set<ae::ObjId> extra_flush_ids;
    if (aux_window_id.is_valid()) {
      extra_flush_ids.insert(aux_window_id);
    }
  if (application.auxiliary.closer.is_valid()) {
      extra_flush_ids.insert(application.auxiliary.closer.id());
    }
    ui::UnloadPresentedWindow(application.auxiliary.window);
    if (ApplicationModelSessionStorage* const session =
            application.EmbeddedUserSession()) {
      ae::Ptr<ae::Obj> flush_root = application.domain->Find(aux_window_id);
      if (!flush_root && application.auxiliary.closer.is_valid()) {
        flush_root = application.domain->Find(application.auxiliary.closer.id());
      }
      if (flush_root) {
        session->disk_user().BeginSaveSession();
        session->FlushReachableSubgraphToDisk(*flush_root,
                                              std::move(extra_flush_ids));
        if (session->disk_user().SaveSessionHadFailure()) {
          WriteFatalStderr(
              "fatal: MinimalApp auxiliary close USER flush failed\n");
          std::abort();
        }
      }
    }
  }
  application.auxiliary.apply_button.Reset();
  application.auxiliary.row_list.Reset();
  application.auxiliary.presenters.apply_button.Reset();
  application.auxiliary.presenters.row_list.Reset();
  application.auxiliary.presenters.closer.Reset();
  application.auxiliary.closer.Reset();
}

void MinimalApplication::LoadAuxiliaryPresentation() {
  ui::LoadPresentedWindow(auxiliary.window);
  auxiliary.apply_button.Load();
  auxiliary.row_list.Load();
  auxiliary.closer.Load();
  auxiliary.presenters.apply_button.Load();
  auxiliary.presenters.row_list.Load();
  auxiliary.presenters.closer.Load();
}

void MinimalApplication::CloseAuxiliaryWindowImpl() {
  if (!auxiliary_presented_open) {
    return;
  }
  SealAuxiliaryWindowClosed(*this);
  NoteMaterializedChange();
}

void MinimalApplication::OpenAuxiliaryWindowImpl() {
  if (auxiliary_presented_open) {
    return;
  }
  LoadAuxiliaryPresentation();
  auxiliary_presented_open = true;
  NoteMaterializedChange();
}

}  // namespace apptraverse::example::minimal
