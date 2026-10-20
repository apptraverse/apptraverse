#ifndef APPTRAVERSE_TESTS_SURFACES_TEST_PUBLICATION_UI_H_
#define APPTRAVERSE_TESTS_SURFACES_TEST_PUBLICATION_UI_H_

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application/model_publication_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/surfaces/surfaces_model.h"

namespace apptraverse {
using SurfacesModelSession = ModelPublicationSession;
}

namespace apptraverse::test {

class SurfacesUiPublicationConsumer {
 public:
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  Application::ptr ui_app;
  ModelObjectProxy proxy{[](ModelObjectProxy::ModelWork) {}};
  std::size_t incremental_applies{0};

  void BindSession(SurfacesModelSession& session);
  void LoadInitialFromSession(SurfacesModelSession& session);
  void ApplyStructuralBytes(std::vector<std::uint8_t> const& bytes);
};

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_SURFACES_TEST_PUBLICATION_UI_H_
