/* Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
 */
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>

#include "apptraverse/directory_domain_storage.h"

int main() {
  namespace fs = std::filesystem;
  auto const parent = fs::current_path();
  auto const root =
      parent /
      ("directory-commit-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!fs::create_directory(root)) return 1;
  bool passed = false;
  {
    apptraverse::DirectoryDomainStorage storage{root};
    ae::DomainQuery query{ae::ObjId{123}, 456, 0};
    std::array<std::uint8_t, 4> expected{1, 3, 5, 7};
    auto writer = storage.Store(query);
    if (writer &&
        writer->Write(
            ae::seri::DataWriteTag{expected.data(), expected.size()}) &&
        writer->Commit()) {
      // Commit must expose accepted data before the writer's destructor runs.
      auto loaded = storage.Load(query);
      std::array<std::uint8_t, 4> actual{};
      passed = loaded.result == ae::DomainLoadResult::kLoaded &&
               loaded.reader &&
               loaded.reader->Read(
                   ae::seri::DataReadTag{actual.data(), actual.size()}) &&
               actual == expected;
    }
  }
  if (root.parent_path() == parent &&
      root.filename().string().starts_with("directory-commit-")) {
    fs::remove_all(root);
  }
  if (!passed) std::cerr << "Committed directory data was not readable\n";
  return passed ? 0 : 1;
}
