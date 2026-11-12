# Telemetry update for the existing product API

This branch starts at the product's original AppTraverse revision
`e71b6cca27efbef197af227edf5a28ea1811fc40`. Its model, runtime partition,
snapshot API and shared-node interfaces remain as that revision defines them.
The independent SDK update on `codex/telemetry-update` covers current SDK main.

Telemetry follows the published `main` branch (verified at `65eda039`). Objects
`80fbb4c`, numeric `9d79bd7`, miscpp `6839624` and Aether `59153adf` retain exact
pins. Both the serialization-scope and stored-factory patches remain owned by
this SDK. Current SDK main's 29-patch chain is not substituted for this baseline.

Directory writers implement the new explicit Commit contract by flushing and
reporting stream errors. Serialization injection and graph-copy paths explicitly
commit their writers. This preserves the existing directory storage behavior;
it does not add a crash-atomic multi-file transaction. The native storage backend
in Objects has its own per-file replacement behavior.

Core model examples and tests remain available. The optional legacy network
demo adapters are selected by `APPTRAVERSE_BUILD_AETHER_DEMOS`, default OFF.
They use an older network API and are not part of the product compatibility
build. The full MSVC x64 Debug build and all 33 CTest cases passed in 98.06
seconds using remote dependencies with no source overrides. This includes
native persistence/restart, Presenter identity and Commit failure coverage.
Other platform runtimes were not rerun. Publication uses the compatibility
branch rather than changing the current SDK main architecture.
