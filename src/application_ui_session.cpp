#include "apptraverse/application_ui_session.h"

#include "apptraverse/object_serialization.h"

namespace apptraverse {

ApplicationUiSession::ApplicationUiSession(
    std::vector<std::uint8_t> const& initial_publication,
    void* presentation_host, ModelObjectProxy& model_proxy)
    : presentation_host_{presentation_host}, model_proxy_{model_proxy} {
  ByteSource in;
  in.data = initial_publication.data();
  in.size = initial_publication.size();
  root_ = LoadInitialPublication(in, ui_domain_, ui_storage_);
  InitializePresenters(*root_, presentation_host_, &model_proxy_);
}

ApplicationUiSession::~ApplicationUiSession() { UnloadPresenters(*root_); }

void ApplicationUiSession::ApplyStructural(
    std::vector<std::uint8_t> const& publication) {
  ByteSource in;
  in.data = publication.data();
  in.size = publication.size();
  ApplyStructuralPublicationAndUpdatePresenters(in, ui_domain_, ui_storage_,
                                                *root_, presentation_host_,
                                                &model_proxy_);
}

ae::Obj& ApplicationUiSession::root() { return *root_; }

ae::Obj const& ApplicationUiSession::root() const { return *root_; }

}  // namespace apptraverse
