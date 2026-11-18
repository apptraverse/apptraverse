#include "apptraverse/application_ui_session.h"

#include "apptraverse/object_serialization.h"

namespace apptraverse {

ApplicationUiSession::ApplicationUiSession(
    std::vector<std::uint8_t> const& initial_publication,
    void* presentation_host, ModelObjectProxy& model_proxy,
    UiPresentersPrepareHook prepare_presenters, bool mount_presentation)
    : presentation_host_{presentation_host},
      model_proxy_{model_proxy},
      prepare_presenters_{std::move(prepare_presenters)} {
  ByteSource in;
  in.data = initial_publication.data();
  in.size = initial_publication.size();
  root_ = LoadInitialPublication(in, ui_domain_, ui_storage_);
  if (mount_presentation) RemountPresentation();
}

ApplicationUiSession::~ApplicationUiSession() { UnmountPresentation(); }

void ApplicationUiSession::UnmountPresentation() {
  if (!presentation_mounted_) return;
  UnloadPresenters(*root_);
  presentation_mounted_ = false;
}

void ApplicationUiSession::RemountPresentation() {
  UnmountPresentation();
  if (prepare_presenters_) {
    prepare_presenters_(*root_);
  }
  InitializePresenters(*root_, presentation_host_, &model_proxy_);
  presentation_mounted_ = true;
}

void ApplicationUiSession::ApplyStructural(
    std::vector<std::uint8_t> const& publication) {
  ByteSource in;
  in.data = publication.data();
  in.size = publication.size();
  if (!presentation_mounted_) {
    // Keep survivor identities alive during hydration even without native views.
    auto keepalive = CaptureStructuralPresentationKeepalive(*root_);
    ApplyStructuralPublication(in, ui_domain_, ui_storage_);
    return;
  }
  ApplyStructuralPublicationAndUpdatePresenters(
      in, ui_domain_, ui_storage_, *root_, presentation_host_, &model_proxy_,
      prepare_presenters_);
}

ae::Obj& ApplicationUiSession::root() { return *root_; }

ae::Obj const& ApplicationUiSession::root() const { return *root_; }

}  // namespace apptraverse
