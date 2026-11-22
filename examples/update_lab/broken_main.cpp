#include <cstdio>
int main() {
  std::fputs("INTENTIONAL_BAD_EXECUTABLE\n", stderr);
  return 86;
}
