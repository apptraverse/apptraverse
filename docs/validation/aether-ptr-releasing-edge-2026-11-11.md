# Releasing a reflected member edge — 2026-11-11

The portable WindowCatalog prefab includes its Close control on every backend.
Its reference to the catalog exposed an existing cyclic-pointer release bug:
window -> Close -> catalog -> holder -> the same window pointer being reset.
The catalog remains owned by the application, but its holder edge is disappearing.

`PtrBase::BuildDecrementGraph` counts the releasing reference once at the root.
The recursive visitor could find that exact member again through the back-link,
count it twice, and treat it as a surviving edge from an externally owned object.
Consequently an otherwise isolated child cycle was retained after Close.

Patch `aether-objects-ptr-releasing-edge-v1.patch` propagates the address of the
releasing `PtrBase` through the traversal and skips only that exact pointer.
Other pointer members referencing the same object remain normal edges. No graph
repair, second reset, registry lookup, or application-specific lifetime pin is used.

## Reproducer and evidence

`tests/aether_ptr_releasing_edge_test.cpp` uses only reflected `ae::Ptr` objects:
an externally retained owner holds a child; the child points to itself and back
to the owner. Clearing the owner's child member must destroy the child cycle
while preserving the owner. Additional cases retain a second owner member,
an external temporary pointer, or both; each real surviving edge must prevent
premature destruction. Destruction counters check exactly-once release.

Executed with Clang in WSL, C++20, exceptions and RTTI disabled:

- Pinned dependency with the first 28 patches: **FAIL**, exit 1,
  `releasing last owner edge must destroy cycle`.
- The same source with patch 29: **PASS**, all four ownership cases,
  `AETHER_PTR_RELEASING_EDGE ok`.
- Isolated application of all 29 repository patches: **PASS**,
  pin `79283b667cabf689da1059d618fe2a22abf51db4`.

Evidence tree: `.artifacts/patch-chain-verification/20261111T095059733237Z`.
Standalone invocation scripts: `.artifacts/ptr-release-{baseline,after}.sh`.
CMake target/test: `apptraverse_aether_ptr_releasing_edge_test`.
Application/platform regression results belong to the portability validation
report; the standalone test does not claim those results.
