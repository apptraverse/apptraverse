"""The portable catalog requires shared runtime factories, never distillation."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_windows_binary import runtime_only_violations


def main():
    report = {'imports': ['KERNEL32.dll', 'USER32.dll']}
    portable_map = '\n'.join([
        'window_catalog_close_button.cpp.obj',
        '?g_apptraverse_registrar_CatalogCloseButton@apptraverse@@',
        '?g_apptraverse_registrar_SetWindowCatalogOrderEvent@apptraverse@@',
    ])
    assert runtime_only_violations(report, '') == []
    assert runtime_only_violations(report, portable_map, True) == []
    assert runtime_only_violations(report, portable_map)
    missing = runtime_only_violations(report, '', True)
    assert len(missing) == 2 and all('Missing portable catalog' in e for e in missing)
    for forbidden in ('window_catalog_graph_mobile_prefab.cpp.obj',
                      'window_catalog_graph_builder.cpp.obj',
                      '?FinalizeDistilledGraph@apptraverse@@',
                      '?g_apptraverse_registrar_WinWindowPresenter@apptraverse@@'):
        assert runtime_only_violations(report, portable_map + '\n' + forbidden, True)
    assert runtime_only_violations({'imports': ['VCRUNTIME140.dll']}, portable_map, True)
    print('WINDOWS_BINARY_CONTRACT_POLICY ok')


if __name__ == '__main__':
    main()
