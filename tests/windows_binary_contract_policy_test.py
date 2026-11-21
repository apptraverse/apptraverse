"""Check PE/map parsing and the portable catalog's linked-runtime boundary."""
import struct
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_windows_binary import inspect, runtime_only_violations


def check_pe_map_parser():
    # Real MSVC x86 maps use eight address digits; PE32+ maps use sixteen.
    # Exercise the whole inspector, including image-base subtraction, sections,
    # imports and folded aliases, so accepting a regex alone cannot pass.
    with TemporaryDirectory(prefix='apptraverse-pe-map-') as directory:
        for bits, magic, base in ((32, 0x10b, 0x400000),
                                   (64, 0x20b, 0x140000000)):
            data = bytearray(0x600)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 0x80)
            data[0x80:0x84] = b'PE\0\0'
            optional = 0x98
            optional_size = 240 if bits == 64 else 224
            struct.pack_into('<HH', data, 0x84, 0x8664 if bits == 64 else 0x14c, 2)
            struct.pack_into('<H', data, 0x94, optional_size)
            struct.pack_into('<H', data, optional, magic)
            struct.pack_into('<Q' if bits == 64 else '<I', data,
                             optional + (24 if bits == 64 else 28), base)
            directories = optional + (112 if bits == 64 else 96)
            struct.pack_into('<II', data, directories + 8, 0x2000, 40)
            for index, (name, rva, offset) in enumerate(
                    ((b'.text', 0x1000, 0x200), (b'.rdata', 0x2000, 0x400))):
                at = optional + optional_size + index * 40
                data[at:at + len(name)] = name
                struct.pack_into('<IIII', data, at + 8, 0x80, rva, 0x200, offset)
            struct.pack_into('<I', data, 0x400 + 12, 0x2050)
            data[0x450:0x45d] = b'KERNEL32.dll\0'
            exe = Path(directory) / f'fixture{bits}.exe'
            map_path = exe.with_suffix('.map')
            exe.write_bytes(data)
            lines = []
            for offset, name, owner in (
                    (0x1000, '?first@@', 'apptraverse.lib:first.cpp.obj'),
                    (0x1010, '?folded@@', 'apptraverse.lib:second.cpp.obj'),
                    (0x1010, '?alias@@', 'aether-objects.lib:object.cpp.obj'),
                    (0x2060, '?constant@@', 'apptraverse.lib:first.cpp.obj')):
                lines.append(f' 0001:{offset:08x} {name} '
                             f'{base + offset:0{bits // 4}x} f i {owner}')
            map_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
            report, _ = inspect(exe, map_path)
            assert report['pe_bits'] == bits, report
            assert report['imports'] == ['KERNEL32.dll'], report
            assert len(report['linked_objects']) == 3, report
            spans = report['largest_map_intervals']
            assert sum(span['estimated_bytes'] for span in spans) == 0x80, spans
            assert any(span['aliases'] == 2 and span['owner'] == '[shared/folded]'
                       for span in spans), spans
            assert report['largest_readonly_map_intervals'][0]['estimated_bytes'] == 0x20
            # A map for the wrong pointer width must fail closed.
            map_path.write_text(' 0001:00000000 ?wrong@@ ' +
                                ('0000000000401000' if bits == 32 else '00401000') +
                                ' f wrong.obj\n', encoding='utf-8')
            try:
                inspect(exe, map_path)
            except ValueError as error:
                assert 'Map has no' in str(error), error
            else:
                raise AssertionError('A map with the wrong address width was accepted')


def main():
    check_pe_map_parser()
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
    for obj, cls in (
            ('win_ui_text_control_presenter.cpp.obj', 'WinTextControlPresenter'),
            ('ui_text_control.cpp.obj', 'UiTextControl'),
            ('window_catalog_language_button.cpp.obj', 'CatalogLanguageButton')):
        for forbidden in (obj, '?g_apptraverse_registrar_' + cls + '@apptraverse@@',
                          '?OnLoad@' + cls + '@apptraverse@@'):
            violations = runtime_only_violations(
                report, portable_map + '\n' + forbidden, True)
            assert len(violations) == 1, (forbidden, violations)
            # These implementations remain available to independent consumers.
            assert runtime_only_violations(report, forbidden) == []
    assert runtime_only_violations({'imports': ['VCRUNTIME140.dll']}, portable_map, True)
    assert runtime_only_violations(report, portable_map +
        '\nwindow_catalog_action_registration.cpp.obj', True)
    for library in ('apptraverse_shared_node', 'apptraverse_shared_aether',
                    'apptraverse_shared_chat_session', 'apptraverse_network_windows',
                    'aether'):
        for owner in (library + ':other.obj', library + '.lib:other.obj'):
            assert runtime_only_violations(report, portable_map + '\n' + owner, True), owner
            assert runtime_only_violations(report, owner) == [], owner
    for obj in ('shared_node.cpp.obj', 'shared_graph.cpp.obj', 'shared_sync.cpp.obj',
                'memory_link.cpp.obj', 'p2p_message_fragments.cpp.obj',
                'chat_session.cpp.obj', 'aether_link.cpp.obj',
                'aether_connection.cpp.obj', 'win_network_observer.cpp.obj'):
        assert runtime_only_violations(report, portable_map + '\n' + obj, True), obj
    for cls, namespace in (('SharedNode', 'apptraverse'),
                           ('SharedSyncRuntime', 'apptraverse'),
                           ('MemoryLink', 'apptraverse'),
                           ('AetherByteLink', 'apptraverse'),
                           ('AetherConnection', 'apptraverse'), ('Client', 'ae'),
                           ('Aether', 'ae'), ('P2pStream', 'ae')):
        symbol = '?OnLoad@' + cls + '@' + namespace + '@@'
        assert runtime_only_violations(report, portable_map + '\n' + symbol, True), symbol
    # Object storage and core connection state are permitted; only the optional
    # SDK/SharedNode implementation crosses this application boundary.
    allowed = '\n'.join(('aether-objects.lib:object.cpp.obj',
                         'aether-miscpp.lib:other.obj', 'aether-tele.lib:other.obj',
                         '?OnLoad@ConnectionState@apptraverse@@',
                         '?OnLoad@Obj@ae@@'))
    assert runtime_only_violations(report, portable_map + '\n' + allowed, True) == []
    print('WINDOWS_BINARY_CONTRACT_POLICY ok')


if __name__ == '__main__':
    main()
