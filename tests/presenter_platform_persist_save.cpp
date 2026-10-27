#include "presenter_platform_persist_shared.h"

#include <iostream>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: presenter_platform_persist_save <state-dir>\n";
    return 1;
  }
  return apptraverse::test::presenter_platform_persist::RunSaveBackend(
      argv[1]);
}
