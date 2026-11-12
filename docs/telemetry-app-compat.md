# Telemetry update for the existing product API

This branch starts at the product's original AppTraverse revision
`e71b6cca27efbef197af227edf5a28ea1811fc40`. Its model, runtime partition,
snapshot API and shared-node interfaces remain as that revision defines them.
The independent SDK update on `codex/telemetry-update` covers current SDK main.

Dependencies are pinned to the published Tele A6, Objects38, numeric9d and
miscpp683 commits and Aether08. The original single serialization-scope patch
remains owned by this SDK. Current SDK main's 29-patch chain is not substituted
for this baseline.

Directory writers implement the new explicit Commit contract by flushing and
reporting stream errors. Serialization injection and graph-copy paths explicitly
commit their writers. This preserves the existing directory storage behavior;
it does not add a crash-atomic multi-file transaction. The native storage backend
in Objects has its own per-file replacement behavior.

Core model examples and tests remain available. The optional legacy network
demo adapters are selected by `APPTRAVERSE_BUILD_AETHER_DEMOS`, default OFF.
They use an older network API and are not part of the product compatibility
build. Validation is in progress; no complete build or test PASS is claimed in
this checkpoint.
