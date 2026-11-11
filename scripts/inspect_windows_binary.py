"""Inspect a PE executable and MSVC map without external Python packages.

Section/file sizes and imports are exact. Map intervals are estimates (padding,
inlining and identical-code folding prevent exact per-function attribution).
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import struct


def inspect(exe, map_path):
    data = exe.read_bytes()
    u16 = lambda pos: struct.unpack_from('<H', data, pos)[0]
    u32 = lambda pos: struct.unpack_from('<I', data, pos)[0]
    pe = u32(0x3c)
    if data[:2] != b'MZ' or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Not a PE executable')
    optional = pe + 24
    is64 = u16(optional) == 0x20b
    image_base = struct.unpack_from('<Q' if is64 else '<I', data,
                                    optional + (24 if is64 else 28))[0]
    directories = optional + (112 if is64 else 96)
    table = optional + u16(pe + 20)
    sections = []
    for index in range(u16(pe + 6)):
        at = table + index * 40
        sections.append(dict(name=data[at:at + 8].rstrip(b'\0').decode(),
                             virtual_bytes=u32(at + 8), rva=u32(at + 12),
                             file_bytes=u32(at + 16), offset=u32(at + 20)))

    def file_offset(rva):
        for section in sections:
            if section['rva'] <= rva < section['rva'] + section['file_bytes']:
                return section['offset'] + rva - section['rva']
        raise ValueError(f'Unmapped RVA {rva:x}')

    def cstring(rva):
        at = file_offset(rva)
        return data[at:data.index(0, at)].decode('ascii')

    imports = []
    for directory, stride, name_offset in [(1, 20, 12), (13, 32, 4)]:
        rva = u32(directories + directory * 8)
        if not rva:
            continue
        at = file_offset(rva)
        while any(data[at:at + stride]):
            name_rva = u32(at + name_offset)
            if directory == 13 and not (u32(at) & 1):
                name_rva -= image_base
            imports.append(cstring(name_rva))
            at += stride

    map_text = map_path.read_text(encoding='utf-8-sig', errors='replace')
    symbols = []
    pattern = re.compile(r'^\s+[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+'
                         r'([0-9a-fA-F]{16})\s+(?:(?:f|i)\s+)*(.*?)\s*$', re.M)
    for name, address, owner in pattern.findall(map_text):
        symbols.append((int(address, 16) - image_base, name, owner))
    if not symbols:
        raise ValueError('Map has no x64 symbols; cannot verify binary contents')
    text_section = next(s for s in sections if s['name'] == '.text')
    begin = text_section['rva']
    end = begin + text_section['virtual_bytes']
    by_address = defaultdict(list)
    for address, name, owner in symbols:
        if begin <= address < end:
            by_address[address].append((name, owner))
    owners = Counter()
    groups = Counter()
    spans = []
    addresses = sorted(by_address)
    for address, limit in zip(addresses, addresses[1:] + [end]):
        aliases = by_address[address]
        sources = {owner for _, owner in aliases}
        # Charge folded code only once. Never add all alias lengths together.
        owner = next(iter(sources)) if len(sources) == 1 else '[shared/folded]'
        size = limit - address
        owners[owner] += size
        groups[owner.split(':', 1)[0]] += size
        spans.append(dict(estimated_bytes=size, owner=owner,
                          symbol=min((n for n, _ in aliases), key=len),
                          aliases=len(aliases)))
    linked_objects = sorted({owner for _, _, owner in symbols})
    readonly = next(s for s in sections if s['name'] == '.rdata')
    readonly_symbols = defaultdict(list)
    for address, name, owner in symbols:
        if readonly['rva'] <= address < readonly['rva'] + readonly['virtual_bytes']:
            readonly_symbols[address].append((name, owner))
    readonly_spans = []
    readonly_addresses = sorted(readonly_symbols)
    for address, limit in zip(readonly_addresses, readonly_addresses[1:] +
                               [readonly['rva'] + readonly['virtual_bytes']]):
        aliases = readonly_symbols[address]
        readonly_spans.append(dict(estimated_bytes=limit - address,
                                   symbol=min((n for n, _ in aliases), key=len),
                                   owners=sorted({owner for _, owner in aliases})))
    return dict(executable=str(exe), file_bytes=len(data),
                header_and_other_bytes=len(data) - sum(s['file_bytes'] for s in sections),
                sections=sections, imports=sorted(set(imports)),
                estimated_code_by_library=groups.most_common(),
                estimated_code_by_object=owners.most_common(25),
                largest_map_intervals=sorted(spans, key=lambda x: -x['estimated_bytes'])[:20],
                largest_readonly_map_intervals=sorted(readonly_spans,
                    key=lambda x: -x['estimated_bytes'])[:15],
                linked_objects=linked_objects), map_text


def runtime_only_violations(report, map_text):
    errors = []
    for dll in report['imports']:
        if re.search(r'(msvcp|msvcr|vcruntime|ucrtbase|api-ms-win-crt|aether|apptraverse)',
                     dll, re.I):
            errors.append('Dynamic runtime dependency: ' + dll)
    forbidden_objects = (
        'distilled_domain_export.cpp.obj', 'distilled_supply_package.cpp.obj',
        'directory_domain_storage.cpp.obj', 'window_catalog_localization.cpp.obj',
        'window_catalog_distillation.cpp.obj', 'window_catalog_graph_builder.cpp.obj',
        'window_catalog_graph_mobile_prefab.cpp.obj',
        'minimal_app_distillation.cpp.obj', 'minimal_app_graph.cpp.obj',
        'minimal_app_graph_builder.cpp.obj', 'minimal_app_bootstrap.cpp.obj',
        'ui_list_row_presenter_registration.cpp.obj', 'win_window_registration.cpp.obj',
        'window_catalog_close_button.cpp.obj',
    )
    for obj in forbidden_objects:
        if obj in map_text:
            errors.append('Unexpected linked object: ' + obj)
    for name in ('FinalizeDistilledGraph@', 'SaveDistilledRoot@',
                 'StabilizeDistilledPersistentSnapshot@', 'BuildWindowCatalogGraph@',
                 'BuildMinimalApplicationGraph@', 'DistillMinimalApplication@',
                 'DistillWindowCatalog@', 'BootstrapMinimalApplicationProfileIfAbsent@'):
        if name in map_text:
            errors.append('Distillation symbol in runtime: ' + name)
    for cls in ('UiListRowPresenter', 'WinWindowPresenter', 'CatalogCloseButton'):
        if 'g_apptraverse_registrar_' + cls + '@' in map_text:
            errors.append('Unused concrete factory: ' + cls)
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    parser.add_argument('--map', type=Path)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--runtime-only', action='store_true',
                        help='Check Windows catalog/minimal static-runtime consumers')
    args = parser.parse_args()
    report, map_text = inspect(args.exe, args.map or args.exe.with_suffix('.map'))
    errors = runtime_only_violations(report, map_text) if args.runtime_only else []
    report['violations'] = errors
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f"{args.exe.name}: {report['file_bytes']} bytes; "
          f"imports: {', '.join(report['imports'])}")
    for section in report['sections']:
        print(f"  {section['name']:10} {section['file_bytes']:8} file bytes")
    for error in errors:
        print('FAIL: ' + error)
    if args.runtime_only and not errors:
        print('PASS: static runtime; no distillation/builders or unused concrete factories')
    return 1 if errors else 0


if __name__ == '__main__':
    raise SystemExit(main())
