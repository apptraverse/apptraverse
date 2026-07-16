#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "aether/obj/obj.h"
#include "aether/obj/domain.h"
#include "aether/domain_storage/ram_domain_storage.h"

namespace apptraverse::test {

class TestObject : public ae::Obj {
  AE_OBJECT(TestObject, Obj, 0)

  TestObject() = default;

 public:
  explicit TestObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::int32_t value{0};
};

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'     \
                << __LINE__ << ")\n";                                     \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

constexpr ae::ObjId::Type kTestObjectId = 100;

}  // namespace

int main() {
  using apptraverse::test::TestObject;

  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  TestObject::ptr obj =
      TestObject::ptr::Create(ae::CreateWith{domain}.with_id(kTestObjectId));

  CHECK(obj);
  CHECK(obj.id().id() == kTestObjectId);
  CHECK(obj->value == 0);

  obj->value = 42;
  CHECK(obj->value == 42);

  return 0;
}
