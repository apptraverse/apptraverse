#ifndef APPTRAVERSE_TESTS_SURFACES_TEST_OBJ_ID_REGISTRY_H_
#define APPTRAVERSE_TESTS_SURFACES_TEST_OBJ_ID_REGISTRY_H_

#include <cstdint>
#include <iostream>
#include <unordered_map>

namespace apptraverse::test {

struct ObjIdSample {
  char const* session{nullptr};
  int iteration{0};
  char const* role{nullptr};
  std::uint32_t class_id{0};
  std::uint32_t obj_id{0};
};

class ObjIdRegistry {
 public:
  void RegisterOrDie(ObjIdSample sample) {
    auto it = seen_.find(sample.obj_id);
    if (it == seen_.end()) {
      seen_.emplace(sample.obj_id, sample);
      return;
    }
    ObjIdSample const& prev = it->second;
    std::cerr << "ObjId collision obj_id=" << sample.obj_id << '\n'
              << "  first session=" << prev.session << " iter=" << prev.iteration
              << " role=" << prev.role << " class_id=" << prev.class_id << '\n'
              << "  second session=" << sample.session << " iter=" << sample.iteration
              << " role=" << sample.role << " class_id=" << sample.class_id
              << '\n';
    std::exit(1);
  }

 private:
  std::unordered_map<std::uint32_t, ObjIdSample> seen_;
};

}  // namespace apptraverse::test

#endif
