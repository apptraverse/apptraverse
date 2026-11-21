"""Exercise live-map discrimination and ELF/Wasm Catalog policy boundaries."""
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_catalog_binary import artifact_info, live_map_lines, violations


CLEAN = [
    '1000 1000 20 1 libwindow_catalog.a(window_catalog_model.cpp.o):(.text)',
    '1020 1020 20 1 apptraverse::WindowCatalog::Load()',
    '1040 1040 10 1 apptraverse::CatalogCloseButton::Load()',
    '1050 1050 10 1 apptraverse::SetWindowCatalogOrderEvent::Apply()',
    '1060 1060 10 1 apptraverse::embedded_distilled::window_catalog::kLayerBytes1',
    '1070 1070 10 1 libaether-objects.a(domain.cpp.o):(.text)',
]
ELF = dict(format='elf', bits=64, imports=['libgtk-3.so.0', 'libc.so.6'])


class ContractTest(unittest.TestCase):
    def test_discarded_and_archive_extraction_are_not_live(self):
        text = '''Archive member included to satisfy reference by file (symbol)
libaether.a(client.cpp.o) (ae::Client::Send)

Discarded input sections
 .text 0x00000000 0x20 libaether.a(client.cpp.o)
 .text.BuildWindowCatalogGraph 0x00000000 0x30 builder.cpp.o

Linker script and memory map
LOAD libaether.a
 .text          0x00001000 0x10 libaether-objects.a(domain.cpp.o)
                0x00001000 apptraverse::WindowCatalog::Load()
 .text.BuildWindowCatalogGraph
                0x00000000 0x0 libcommon.a(window_catalog_graph_builder.cpp.o)
 .rodata        0x00002000 0x20 fixture.cpp.o
                0x00002000 apptraverse::CatalogCloseButton::Load()
                0x00002008 apptraverse::SetWindowCatalogOrderEvent::Apply()
                0x00002010 apptraverse::embedded_distilled::window_catalog::kLayerBytes1
'''
        flavor, rows = live_map_lines(text)
        self.assertEqual(flavor, 'gnu')
        self.assertEqual(violations(ELF, rows), [])

    def test_gnu_multiline_live_section_is_checked(self):
        _, rows = live_map_lines('''Linker script and memory map
 .text._ZN11apptraverse24FinalizeDistilledGraphEv
                0x00001000 0x20 /tmp/program.ltrans0.ltrans.o
                0x00001000 apptraverse::WindowCatalog::Load()
''')
        self.assertTrue(any('FinalizeDistilledGraph' in error
                            for error in violations(ELF, rows)))

    def test_gold_memory_map_skips_prior_discarded_sections(self):
        text = '''Discarded input sections
 .text._ZN2ae6Client4SendEv
                0x0000000000000000 0x13 /tmp/lto-llvm-discarded.o

Memory map

 ** file header
                0x0000000000000000 0x40
 .text          0x000000000001c888 0x16 /tmp/lto-llvm-live.o
                0x000000000001c888 apptraverse::WindowCatalog::Load()
 .text._ZN11apptraverse10SharedNode4SendEv
                0x000000000001c8a0 0x13 /tmp/lto-llvm-live.o
'''
        flavor, rows = live_map_lines(text)
        self.assertEqual(flavor, 'gold')
        self.assertFalse(any('discarded' in row for row in rows))
        self.assertTrue(any('SharedNode' in error
                            for error in violations(ELF, rows, network_only=True)))

    def test_lld_elf_and_wasm_rows(self):
        for header in ('VMA LMA Size Align Out In Symbol',
                       'Addr Off Size Out In Symbol'):
            flavor, rows = live_map_lines(header + '\n' + '\n'.join(CLEAN))
            self.assertEqual(flavor, 'lld')
            self.assertEqual(violations(ELF, rows), [])

    def test_network_identities_and_lto_symbols(self):
        for bad in ('libaether.a(client.cpp.o):(.text)',
                    '/build/libapptraverse_shared_node.a(shared_node.cpp.o)',
                    'apptraverse::SharedSyncRuntime::Receive()',
                    '_ZN2ae6Client4SendEv', '_ZN11apptraverse10SharedNode4SendEv'):
            errors = violations(ELF, CLEAN + ['1000 1000 20 1 ' + bad])
            self.assertTrue(any('network' in error.lower() for error in errors), bad)

    def test_live_builder_and_unused_control_rejected(self):
        for bad in ('libcommon.a(window_catalog_graph_builder_linux.cpp.o)',
                    'BuildWebWindowCatalogGraph(ae::Domain&)',
                    'AppendMobileCloseButtonToWindowPrefab(ae::Domain&)',
                    'InstallWindowCatalogLocalization(apptraverse::WindowCatalog&)',
                    '_ZN11apptraverse18CatalogLanguageButton4LoadEv',
                    'apptraverse::ui::AndroidTextControlPresenter::Apply()'):
            self.assertTrue(violations(ELF, CLEAN + ['1000 1000 20 1 ' + bad]), bad)

    def test_portable_schema_and_supply_are_required(self):
        for index in (2, 3, 4):
            self.assertTrue(violations(ELF, CLEAN[:index] + CLEAN[index + 1:]))

    def test_headless_model_fixture_can_build_but_cannot_network(self):
        rows = ['1000 1000 20 1 BuildWindowCatalogGraph(ae::Domain&)']
        self.assertEqual(violations(ELF, rows, network_only=True), [])
        self.assertTrue(violations(ELF, rows + ['1000 1000 20 1 libaether.a(client.cpp.o)'],
                                   network_only=True))

    def test_dynamic_cpp_runtime_rejected(self):
        for library in ('libstdc++.so.6', 'libgcc_s.so.1', 'libc++_shared.so',
                        'libapptraverse_shared_node.so', 'libaether.so'):
            report = dict(ELF, imports=ELF['imports'] + [library])
            self.assertTrue(any('Dynamic' in error for error in violations(report, CLEAN)))
        self.assertEqual(violations(dict(ELF, imports=['libaether-objects.so']), CLEAN), [])

    def test_missing_or_unknown_map_fails_closed(self):
        for text in ('', 'Discarded input sections\n .text 0x0 0x20 libaether.a',
                     'Linker script and memory map\nLOAD libaether.a'):
            with self.assertRaises(ValueError):
                live_map_lines(text)

    def test_elf_dynamic_needed_and_wasm_header(self):
        with TemporaryDirectory() as directory:
            binary = Path(directory) / 'consumer'
            for bits in (32, 64):
                data = bytearray(0x300)
                data[:6] = b'\x7fELF' + bytes([2 if bits == 64 else 1, 1])
                section_format = '<IIQQQQIIQQ' if bits == 64 else '<IIIIIIIIII'
                section_size = struct.calcsize(section_format)
                struct.pack_into('<Q' if bits == 64 else '<I', data,
                                 40 if bits == 64 else 32, 0x80)
                struct.pack_into('<HH', data, 58 if bits == 64 else 46, section_size, 3)
                string = b'\0libstdc++.so.6\0'
                struct.pack_into(section_format, data, 0x80 + section_size,
                                 0, 3, 0, 0, 0x200, len(string), 0, 0, 1, 0)
                dynamic_format = '<qQ' if bits == 64 else '<iI'
                dynamic_size = struct.calcsize(dynamic_format)
                struct.pack_into(section_format, data, 0x80 + section_size * 2,
                                 0, 6, 0, 0, 0x240, dynamic_size * 2, 1, 0, 8, dynamic_size)
                data[0x200:0x200 + len(string)] = string
                struct.pack_into(dynamic_format, data, 0x240, 1, 1)
                binary.write_bytes(data)
                report = artifact_info(binary)
                self.assertEqual(report['bits'], bits)
                self.assertEqual(report['imports'], ['libstdc++.so.6'])
            binary.write_bytes(b'\0asm\x01\0\0\0')
            self.assertEqual(artifact_info(binary)['format'], 'wasm')


if __name__ == '__main__':
    unittest.main()
