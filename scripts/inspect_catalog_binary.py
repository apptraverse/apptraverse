"""Check ELF/Wasm Catalog consumers against their GNU ld or LLD live link map.

Discarded input sections, archive extraction reasons and LOAD directives are not
linked-code evidence. LTO can erase object provenance, so live symbol names are
checked independently. This verifies the emitted artifact, not runtime behavior.
"""
import argparse
import json
from pathlib import Path
import re
import struct


def live_map_lines(text):
    """Return only allocated input sections/symbols, never discarded sections."""
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if re.search(r'\b(?:VMA\s+LMA|Addr\s+Off)\s+Size\b', line):
            # LLD ELF and wasm maps contain only selected output rows. Symbol
            # aliases may have size zero and still identify linked code/data.
            selected = [row for row in lines[index + 1:]
                        if re.match(r'^\s*[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+'
                                    r'[0-9a-fA-F]+\s+', row)]
            if not selected:
                raise ValueError('LLD map has no output rows')
            return 'lld', selected
    marker = next((i for i, line in enumerate(lines)
                   if line.strip() in ('Linker script and memory map', 'Memory map')), None)
    if marker is None:
        raise ValueError('Unrecognized link map (expected GNU ld or LLD)')
    selected = []
    pending_section = ''
    for line in lines[marker + 1:]:
        if line.startswith(('LOAD ', 'START GROUP', 'END GROUP', 'OUTPUT(',
                            'Cross Reference Table')):
            pending_section = ''
            continue
        if re.match(r'^\s+\.\S+\s*$', line):
            pending_section = line.strip()
            continue
        section = re.match(r'^\s*(?:\.\S+\s+)?0x[0-9a-fA-F]+\s+'
                           r'0x([0-9a-fA-F]+)\s*(.*)$', line)
        if section:
            if int(section[1], 16):
                selected.append(pending_section + ' ' + line)
            pending_section = ''
            continue
        pending_section = ''
        symbol = re.match(r'^\s+0x([0-9a-fA-F]+)\s+(\S.*)$', line)
        if symbol and int(symbol[1], 16) and not re.search(
                r'\b(?:PROVIDE|ASSERT|LOADADDR|SIZEOF|ALIGN)\b| = ', symbol[2]):
            selected.append(line)
    if not selected:
        raise ValueError('GNU ld map has no live sections/symbols')
    return ('gold' if lines[marker].strip() == 'Memory map' else 'gnu'), selected


def artifact_info(path):
    data = path.read_bytes()
    report = dict(artifact=str(path), file_bytes=len(data), imports=[])
    if data[:4] == b'\0asm':
        if data[4:8] != b'\x01\0\0\0':
            raise ValueError('Unsupported WebAssembly version')
        report['format'] = 'wasm'
        return report
    if data[:4] != b'\x7fELF' or data[4] not in (1, 2) or data[5] not in (1, 2):
        raise ValueError('Expected an ELF or WebAssembly artifact')
    bits = 64 if data[4] == 2 else 32
    endian = '<' if data[5] == 1 else '>'
    offset, entry_size, count = (struct.unpack_from(endian + 'Q', data, 40)[0],
                               *struct.unpack_from(endian + 'HH', data, 58)) \
        if bits == 64 else (struct.unpack_from(endian + 'I', data, 32)[0],
                            *struct.unpack_from(endian + 'HH', data, 46))
    section_format = endian + ('IIQQQQIIQQ' if bits == 64 else 'IIIIIIIIII')
    sections = [struct.unpack_from(section_format, data, offset + i * entry_size)
                for i in range(count)]
    needed = []
    for section in sections:
        if section[1] != 6:  # SHT_DYNAMIC; sh_link points to its string table.
            continue
        strings = sections[section[6]]
        dynamic_format = endian + ('qQ' if bits == 64 else 'iI')
        stride = struct.calcsize(dynamic_format)
        for pos in range(section[4], section[4] + section[5], stride):
            tag, value = struct.unpack_from(dynamic_format, data, pos)
            if tag == 1:  # DT_NEEDED
                start = strings[4] + value
                needed.append(data[start:data.index(0, start)].decode('ascii'))
    report.update(format='elf', bits=bits, imports=sorted(set(needed)))
    return report


def violations(report, rows, network_only=False):
    errors = []
    text = '\n'.join(rows)
    if not re.search(r'WindowCatalog|window_catalog', text):
        errors.append('Map contains no recognizable WindowCatalog output')
    if report['format'] == 'elf' and report['bits'] != 64:
        errors.append('Minimum native consumer must be 64-bit')
    # Complete archive identities: libaether-objects.a is ordinary object storage,
    # while libaether.a is the separately enabled network SDK.
    libraries = ('apptraverse_shared_node', 'apptraverse_shared_aether',
                 'apptraverse_shared_chat_session', 'apptraverse_network_windows',
                 'aether')
    forbidden_imports = ('stdc++', 'c++_shared', 'gcc_s', 'apptraverse') + libraries
    for library in report['imports']:
        if any(re.match(r'^lib' + re.escape(name) + r'\.so(?:\.|$)', library, re.I)
               for name in forbidden_imports):
            errors.append('Dynamic C++/AppTraverse/network dependency: ' + library)
    for library in libraries:
        if re.search(r'(?<![\w.-])(?:lib)?' + re.escape(library) +
                     r'\.(?:a|so|lib)(?=[(\s:]|$)', text, re.I):
            errors.append('Forbidden catalog network library: ' + library)
    objects = ('shared_node', 'shared_graph', 'shared_sync', 'memory_link',
               'p2p_message_fragments', 'chat_session', 'aether_link',
               'aether_connection', 'win_network_observer')
    if not network_only:
        objects += ('distilled_domain_export', 'distilled_supply_package',
                    'directory_domain_storage', 'window_catalog_localization',
                    'window_catalog_distillation', 'window_catalog_graph_builder',
                    'window_catalog_graph_builder_linux', 'window_catalog_graph_builder_web',
                    'window_catalog_graph_builder_android',
                    'window_catalog_graph_mobile_prefab', 'window_catalog_menu_builder',
                    'window_catalog_action_registration', 'window_catalog_language_button',
                    'ui_text_control', 'linux_ui_text_control_presenter',
                    'web_ui_text_control_presenter', 'android_ui_text_control_presenter',
                    'android_ui_text_presenter')
    for obj in objects:
        if re.search(r'(?<![\w.-])' + re.escape(obj) + r'\.cpp\.(?:o|obj)\b', text):
            errors.append('Forbidden linked object: ' + obj)
    # Match both demangled names and Itanium C++ ABI namespace/name components.
    for namespace, cls in (('apptraverse', 'SharedNode'),
                           ('apptraverse', 'SharedSyncRuntime'),
                           ('apptraverse', 'MemoryLink'),
                           ('apptraverse', 'AetherByteLink'),
                           ('apptraverse', 'AetherConnection'),
                           ('ae', 'Client'), ('ae', 'Aether'), ('ae', 'P2pStream')):
        if re.search(r'\b' + namespace + r'::' + cls + r'\b|' +
                     str(len(namespace)) + namespace + str(len(cls)) + cls +
                     r'(?=[A-Z0-9_])', text):
            errors.append('Forbidden catalog network symbol: ' + namespace + '::' + cls)
    if not network_only:
        for symbol in ('FinalizeDistilledGraph', 'SaveDistilledRoot',
                       'StabilizeDistilledPersistentSnapshot', 'BuildWindowCatalogGraph',
                       'BuildWebWindowCatalogGraph', 'AppendMobileCloseButtonToWindowPrefab',
                       'DistillWindowCatalog', 'ComposeCatalogMenu',
                       'CreateCatalogLocalizedString', 'MakeCatalogLocalizedString',
                       'InstallWindowCatalogLocalization', 'CreateMainWindowTitle'):
            if symbol in text:
                errors.append('Distillation/builder symbol in consumer: ' + symbol)
        if not re.search(r'embedded_distilled(?:::|\d)|kLayerBytes', text):
            errors.append('Missing linked embedded supply data')
        for cls in ('CatalogCloseButton', 'SetWindowCatalogOrderEvent'):
            if cls not in text:
                errors.append('Missing portable catalog implementation: ' + cls)
        for cls in ('CatalogLanguageButton', 'UiTextControl',
                    'LinuxTextControlPresenter', 'WebTextControlPresenter',
                    'AndroidTextControlPresenter'):
            if re.search(r'\b' + cls + r'\b|\d' + cls + r'(?=[A-Z0-9_])', text):
                errors.append('Unused catalog implementation symbol: ' + cls)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifact', type=Path)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--network-only', action='store_true',
                        help='Headless model fixture may deliberately build its graph')
    args = parser.parse_args()
    try:
        report = artifact_info(args.artifact)
        flavor, rows = live_map_lines(args.map.read_text(encoding='utf-8-sig', errors='replace'))
        report.update(map=str(args.map), map_format=flavor, live_rows=len(rows),
                      contract='network-only' if args.network_only else 'embedded-consumer')
        report['violations'] = violations(report, rows, args.network_only)
    except (ValueError, IndexError, OSError, struct.error) as error:
        print('FAIL: ' + str(error))
        return 1
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"{args.artifact.name}: {report['file_bytes']} bytes; {flavor} map; "
          f"{len(rows)} live rows; imports: {', '.join(report['imports'])}")
    for error in report['violations']:
        print('FAIL: ' + error)
    if not report['violations']:
        print('PASS: ' + report['contract'] + ' binary contract')
    return bool(report['violations'])


if __name__ == '__main__':
    raise SystemExit(main())
