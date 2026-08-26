#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

#include "aether/clock.h"
#include "aether/obj/domain.h"
#include "aether/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

constexpr ae::ObjId::Type kProbeNodeId = 100000;

void TestDirectoryDomainStorageRoundtrip() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_directory_domain_storage_test";
  std::filesystem::remove_all(root);

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};
    auto node =
        Node::ptr::Create(ae::CreateWith{domain}.with_id(kProbeNodeId));
    CHECK(node.is_valid());
    node.Save();
    CHECK(node.domain() == &domain);
  }

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};
    auto node =
        Node::ptr::Declare(ae::CreateWith{domain}.with_id(kProbeNodeId));
    node.Load();
    CHECK(node.is_loaded());
    CHECK(node.domain() == &domain);
  }

  std::filesystem::remove_all(root);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestDirectoryDomainStorageRoundtrip();
  std::cout << "directory_domain_storage_test OK\n";
  return 0;
}
