#ifndef APPTRAVERSE_APPLICATION_UI_SESSION_H_
#define APPTRAVERSE_APPLICATION_UI_SESSION_H_

#include <cstdint>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

class ApplicationUiSession {
 public:
  ApplicationUiSession(std::vector<std::uint8_t> const& initial_publication,
                       void* presentation_host,
                       ModelObjectProxy& model_proxy);
  ~ApplicationUiSession();

  ApplicationUiSession(ApplicationUiSession const&) = delete;
  ApplicationUiSession& operator=(ApplicationUiSession const&) = delete;

  void ApplyStructural(std::vector<std::uint8_t> const& publication);

  // Unload native views and rebuild them against the current presentation host
  // (same UI mirror graph and presenter identities).
  void RemountPresentation();

  ae::Obj& root();
  ae::Obj const& root() const;

 private:
  ae::RamDomainStorage ui_storage_;
  ae::Domain ui_domain_{ui_storage_};
  ae::Ptr<ae::Obj> root_;
  void* presentation_host_;
  ModelObjectProxy& model_proxy_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_APPLICATION_UI_SESSION_H_
