#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/idomain_storage.h"

#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void WriteBytes(ae::IDomainStorage& storage, ae::DomainQuery const& query,
                std::vector<std::uint8_t> const& bytes) {
  auto writer = storage.Store(query);
  CHECK(writer);
  CHECK(writer->Write(ae::seri::DataWriteTag{bytes.data(), bytes.size()}));
}

std::vector<std::uint8_t> ReadAll(ae::DomainLoad load) {
  CHECK(load.result == ae::DomainLoadResult::kLoaded);
  CHECK(load.reader);
  std::vector<std::uint8_t> out;
  for (;;) {
    std::uint8_t byte = 0;
    auto const result = load.reader->Read(ae::seri::DataReadTag{&byte, 1});
    if (!result) {
      break;
    }
    out.push_back(byte);
  }
  return out;
}

void TestOverlayPrefersOverlayAndFallsBackToBacking() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};

  ae::ObjId const id{42};
  ae::DomainQuery const backing_only{id, 7, 0};
  ae::DomainQuery const both{id, 9, 0};

  WriteBytes(backing, backing_only, {1, 2, 3});
  WriteBytes(backing, both, {10, 11});
  WriteBytes(overlay, both, {20, 21, 22});

  auto load_backing_only = overlay.Load(backing_only);
  CHECK(ReadAll(std::move(load_backing_only)) ==
        (std::vector<std::uint8_t>{1, 2, 3}));

  auto load_overlay = overlay.Load(both);
  CHECK(ReadAll(std::move(load_overlay)) ==
        (std::vector<std::uint8_t>{20, 21, 22}));

  auto backing_both = backing.Load(both);
  CHECK(ReadAll(std::move(backing_both)) ==
        (std::vector<std::uint8_t>{10, 11}));

  auto classes = overlay.Enumerate(id);
  std::set<std::uint32_t> unique{classes.begin(), classes.end()};
  CHECK(unique.count(7) == 1);
  CHECK(unique.count(9) == 1);
  CHECK(unique.size() == 2);

  overlay.CleanUp();
  auto after_cleanup = overlay.Load(both);
  CHECK(ReadAll(std::move(after_cleanup)) ==
        (std::vector<std::uint8_t>{10, 11}));
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestOverlayPrefersOverlayAndFallsBackToBacking();
  std::cout << "overlay_domain_storage_test OK\n";
  return 0;
}
