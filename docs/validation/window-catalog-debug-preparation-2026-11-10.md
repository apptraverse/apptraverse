# Window Catalog: Debug-owned initial-state preparation

## Contract

The regular Windows Debug application always prepares a fresh initial graph.
It never loads the previous USER profile. Each invocation creates a distinct
`debug-run-<id>` beneath `--state-dir`, with initial object/class/version files,
an embedded header, and a new writable `user` directory. Existing runs are not
deleted or overwritten. After preparation, the construction domain is destroyed
and the application uses the normal loader with USER over initial BACKING.

Preparation writes development initial-state files at startup. USER writes
remain confined to explicit window unload and application shutdown.

The standalone Windows WindowCatalog exporter target has been removed.
The regular Debug executable supports `--distill-only <output-directory>` for
build-time preparation without opening the GUI. It writes ordinary individual
layer files plus `embedded_distilled_window_catalog_supply.h`; the header is an
embedding representation, not the USER storage format or a pack file.

The load-only executable and MinSizeRel consume a prepared embedded header.
They retain USER restoration. The same header must be used by consumers that
are expected to share a profile. This workflow does not claim that independent
fresh distillation runs produce identical IDs or bytes.

## Windows Ninja build order

1. Build Debug `win32_window_catalog_demo_load_only`. Its dependency builds the
   regular Debug application, invokes its preparation mode, then embeds the
   generated supply into the load-only consumer.
2. Configure/build MinSizeRel. By default it copies the header prepared in
   `build/win64-ninja-msvc-debug/examples/surfaces_demo/generated_distilled`.
   `APPTRAVERSE_WINDOW_CATALOG_DEBUG_SUPPLY_HEADER` selects a different prepared
   header explicitly. It must already exist; MinSizeRel never distills a graph.
3. After changing the initial graph, rebuild the Debug supply before rebuilding
   its consumers. The two build trees do not automatically rebuild each other.

This configuration and its verification use single-configuration Ninja/MSVC.
Visual Studio multi-configuration generation is not covered by this change.

## Executed verification before automatic-scope integration

- Debug Win32 acceptance: PASS, 29.43 seconds. A new case launches normal Debug
  twice against one development directory, adds a window each time, and checks
  that each launch starts with zero children. It also checks that both separate
  run directories contain saved USER and an embedded header.
- Existing restart, delete, localization and required-graph negative scenarios
  now run against the load-only consumer. Their checks remain enabled.
- MinSizeRel Win32 acceptance: PASS, 18.37 seconds, including persistent restart
  and the existing negative scenarios.
- Debug-prepared and MinSizeRel-consumed headers have identical SHA-256:
  `4C8279CB8B6B9D06D120229C6C6420E69765CD4BA06C85CDB2E5066012A7BD8D`.

Logs: `.artifacts/debug-bootstrap-acceptance.log` and
`.artifacts/minsizerel-debug-supply-acceptance.log`. Subsequent object-scope
integration requires a new final regression; these are scoped intermediate
results, not completion of the whole automatic-cloning task.
