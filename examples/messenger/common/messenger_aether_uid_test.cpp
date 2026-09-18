#include <cstdlib>
#include <iostream>
#include <string>

#include "messenger_aether_uid.h"

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

int main() {
  std::string out;
  CHECK(!apptraverse::TryCanonicalizeAetherUid("", out));
  CHECK(!apptraverse::TryCanonicalizeAetherUid("not-a-uid", out));
  CHECK(!apptraverse::TryCanonicalizeAetherUid(
      "00000000-0000-0000-0000-000000000000", out));
  CHECK(!apptraverse::TryCanonicalizeAetherUid(
      "3ac93165-3d37-4970-87a6-fa4ee27744e", out));
  CHECK(apptraverse::TryCanonicalizeAetherUid(
      "3ac93165-3d37-4970-87a6-fa4ee27744e4", out));
  CHECK(out == "3ac93165-3d37-4970-87a6-fa4ee27744e4");
  CHECK(apptraverse::TryCanonicalizeAetherUid(
      "  3AC93165-3D37-4970-87A6-FA4EE27744E4  ", out));
  CHECK(out == "3ac93165-3d37-4970-87a6-fa4ee27744e4");
  std::cout << "messenger_aether_uid_test ok\n";
  return 0;
}
