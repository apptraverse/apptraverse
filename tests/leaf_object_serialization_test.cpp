#include <cstdlib>
#include <iostream>
#include <string>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

namespace apptraverse::test {

class LeafObject : public ae::Obj {
  AE_OBJECT(LeafObject, Obj, 0)

 protected:
  LeafObject() = default;

 public:
  explicit LeafObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  using apptraverse::test::LeafObject;

  ae::RamDomainStorage storage;

  ae::Domain domain1{ae::Now(), storage};
  LeafObject::ptr original =
      LeafObject::ptr::Create(ae::CreateWith{domain1}.with_id(100));
  CHECK(static_cast<bool>(original));
  CHECK(original.id().id() == 100);

  original->name = "Alice";
  original.Save();

  ae::Domain domain2{ae::Now(), storage};
  LeafObject::ptr declared =
      LeafObject::ptr::Declare(ae::CreateWith{domain2}.with_id(100));

  CHECK(declared.is_valid());
  CHECK(!declared.is_loaded());

  declared.Load();

  CHECK(static_cast<bool>(declared));
  CHECK(declared.is_loaded());
  CHECK(declared.id() == original.id());
  CHECK(declared.id().id() == 100);
  CHECK(declared->name == "Alice");
  CHECK(original.Load().get() != declared.Load().get());

  return EXIT_SUCCESS;
}
