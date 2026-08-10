#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

#include "aether/aether_app.h"
#include "aether/clock.h"
#include "aether/obj/obj.h"

#include "apptraverse/app.h"
#include "apptraverse/application_ids.h"
#include "apptraverse/directory_domain_storage.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void TestDirectoryDomainStorageOneDomain() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_test";
  std::filesystem::remove_all(root);

  {
    auto aether_app = ae::AetherApp::Construct(ae::AetherAppContext{[root]() {
      return std::make_unique<DirectoryDomainStorage>(root);
    }});
    CHECK(aether_app.get() != nullptr);

    ae::Domain& domain = aether_app->domain();
    CHECK(aether_app->aether().is_valid());
    CHECK(aether_app->aether().id().id() == 1);

    auto app = App::ptr::Create(
        ae::CreateWith{domain}.with_id(ToObjId(ApplicationObjId::Application)));
    CHECK(app.is_valid());
    app.Save();

    CHECK(app.domain() == &domain);
    std::cout << "SINGLE_DOMAIN_READY aether_domain=" << &domain
              << " app_domain=" << app.domain()
              << " aether_root=" << aether_app->aether().id().id()
              << " app=" << ToObjId(ApplicationObjId::Application) << '\n';

    for (int i = 0; i < 3; ++i) {
      (void)aether_app->Update(ae::Now());
    }
  }

  {
    auto aether_app = ae::AetherApp::Construct(ae::AetherAppContext{[root]() {
      return std::make_unique<DirectoryDomainStorage>(root);
    }});
    ae::Domain& domain = aether_app->domain();
    CHECK(aether_app->aether().is_valid());
    CHECK(aether_app->aether().id().id() == 1);

    auto app = App::ptr::Declare(ae::CreateWith{domain}.with_id(
        ToObjId(ApplicationObjId::Application)));
    app.Load();
    CHECK(app.is_loaded());
    CHECK(app.domain() == &domain);
  }

  std::filesystem::remove_all(root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestDirectoryDomainStorageOneDomain();
  std::cout << "directory_domain_storage_test OK\n";
  return 0;
}
