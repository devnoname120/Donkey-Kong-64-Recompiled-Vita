#!/usr/bin/env python3
"""Check the source inventory behind the Vita upstream-patch audit.

This checks accounting and source drift, not semantic equivalence or game
compatibility. --objects additionally compares freshly built MIPS patch objects.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'docs/vita_upstream_patches.json'
LEXICAL = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
DISPOSITIONS = {'equivalent', 'partial', 'retained_original', 'missing', 'review_required', 'upstream_disabled'}


def source_definitions():
    result = {}
    for path in sorted((ROOT / 'patches').glob('*.c')):
        raw = path.read_text()
        masked = LEXICAL.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), raw)
        for marker in re.finditer(r'\bRECOMP_(?:FORCE_)?PATCH\b', masked):
            opening = masked.find('(', marker.end())
            name = re.search(r'(\w+)\s*$', masked[marker.end():opening])
            if name is None:
                raise ValueError(f'Cannot parse patch declaration in {path}')
            depth = 0
            for closing in range(opening, len(masked)):
                if masked[closing] == '(':
                    depth += 1
                elif masked[closing] == ')':
                    depth -= 1
                    if depth == 0:
                        break
            brace = closing + 1
            while brace < len(masked) and masked[brace].isspace():
                brace += 1
            if brace == len(masked) or masked[brace] != '{':
                continue  # Declaration without a definition.
            source = path.relative_to(ROOT).as_posix()
            key = source + '::' + name.group(1)
            if key in result:
                raise ValueError(f'Duplicate definition: {key}')
            result[key] = {'source': source, 'function': name.group(1), 'line': raw.count('\n', 0, marker.start()) + 1}
    return result


def compiled_definitions():
    result = set()
    for path in sorted((ROOT / 'patches').glob('*.c')):
        obj = path.with_suffix('.o')
        data = obj.read_bytes()
        if data[:6] != b'\x7fELF\x01\x02':
            raise ValueError(f'Expected a 32-bit big-endian MIPS object: {obj}')
        header = struct.unpack_from('>16sHHIIIIIHHHHHH', data)
        if header[2] != 8:
            raise ValueError(f'Not a MIPS object: {obj}')
        sections = [struct.unpack_from('>10I', data, header[6] + i * header[11]) for i in range(header[12])]
        strings = sections[header[13]]
        names = data[strings[4]:strings[4] + strings[5]]
        section_names = [names[s[0]:].split(b'\0', 1)[0].decode() for s in sections]
        for section in sections:
            if section[1] != 2:
                continue
            strings = sections[section[6]]
            names = data[strings[4]:strings[4] + strings[5]]
            for at in range(section[4], section[4] + section[5], section[9]):
                name, _, _, info, _, index = struct.unpack_from('>IIIBBH', data, at)
                if info & 15 == 2 and index < len(sections) and section_names[index] in {'.recomp_patch', '.recomp_force_patch'}:
                    result.add(path.relative_to(ROOT).as_posix() + '::' + names[name:].split(b'\0', 1)[0].decode())
    return result


def check_manifest(manifest, objects=False):
    actual = source_definitions()
    records = manifest['patches']
    expected = {r['source'] + '::' + r['function']: r for r in records}
    if len(expected) != len(records):
        raise ValueError('Duplicate audit records')
    if actual.keys() != expected.keys():
        raise ValueError(f'Inventory changed; new={sorted(actual.keys() - expected.keys())}, removed={sorted(expected.keys() - actual.keys())}')
    support = {p.relative_to(ROOT).as_posix() for p in (ROOT / 'patches').rglob('*') if p.is_file() and p.suffix in {'.c', '.h', '.ld'}}
    support.add('patches/Makefile')
    if support != manifest['source_files'].keys():
        raise ValueError('Patch/support file inventory changed; update the audit')
    for source, digest in manifest['source_files'].items():
        if hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Upstream patch source changed: {source}; review before updating its recorded hash')
    for source, digest in manifest['reference_headers'].items():
        if hashlib.sha256((ROOT / source).read_bytes()).hexdigest() != digest:
            raise ValueError(f'Patch reference header changed: {source}; review its effect on the audit')
    for key, record in expected.items():
        if record['disposition'] not in DISPOSITIONS:
            raise ValueError(f'Unknown disposition: {key}')
        for field in ['purpose', 'reason', 'evidence', 'next_action']:
            if not record.get(field, '').strip():
                raise ValueError(f'Missing {field}: {key}')
        if bool(record['compiled']) == (record['disposition'] == 'upstream_disabled'):
            raise ValueError(f'Inconsistent upstream build status: {key}')
    if objects:
        compiled = compiled_definitions()
        expected_compiled = {key for key, r in expected.items() if r['compiled']}
        if compiled != expected_compiled:
            raise ValueError(f'Compiled inventory changed; new={sorted(compiled - expected_compiled)}, removed={sorted(expected_compiled - compiled)}')
    return actual


def check_recompiled(manifest, directory):
    text = (directory / 'recomp_overlays.inl').read_text()
    tables = re.findall(r'static FuncEntry section_\d+_recomp(?:_force)?_patch_funcs\[\] = \{(.*?)\n\};', text, re.S)
    found = {name for table in tables for name in re.findall(r'\.func = (\w+)', table)}
    expected = {r['function'] for r in manifest['patches'] if r['compiled']}
    if found != expected:
        raise ValueError(f'Recompiled patch registration changed; new={sorted(found - expected)}, removed={sorted(expected - found)}')


def render(manifest, definitions):
    counts = Counter(r['disposition'] for r in manifest['patches'])
    lines = ['# Upstream patch accounting for the Vita port', '',
        '**This audit is in progress. Inventory coverage is not proof that every patch has been ported or every omission is safe.**', '',
        f"Source baseline: `{manifest['upstream_revision']}`. The default upstream patch build contains **{sum(r['compiled'] for r in manifest['patches'])} replacement functions**; one additional source definition is disabled in that build.", '',
        'The Vita build returns before `PatchesLib` and patch registration. Its runtime uses the ROM-generated functions, `us.vita.toml` hooks, native helpers and the reduced RT64 backend. Each row below accounts for a source replacement; a partial port can carry a gameplay correction while retaining original graphics or menu behavior.', '',
        '## Coverage', '',
        '| Disposition | Definitions |', '|---|---:|']
    lines.extend(f'| {name} | {count} |' for name, count in sorted(counts.items()))
    lines += ['', 'A `review_required` or `missing` row is an open porting issue. `retained_original` records an intentional decision for the current renderer/features, with its proof boundary in the evidence column. `partial` must not be read as full equivalence.', '',
        '## Source and build verification', '',
        '`python3 tools/audit_vita_patches.py --check` checks source/reference-header hashes, definition coverage and complete accounting fields. It does not run gameplay tests. After building the upstream MIPS patch objects with `patches/Makefile`, add `--objects` to compare the actual `.recomp_patch` function symbols. After strict patch recompilation, `--recompiled OUTPUT_DIRECTORY` checks the generated registration metadata. `--require-resolved` additionally fails while any review or missing-port rows remain. Passing that option confirms a recorded disposition for every function; it does not resolve partial integrations or certify runtime correctness.', '',
        'The inventory was checked against actual MIPS objects, a linked patch ELF, and strict patch recompilation. All 162 names appear in the generated registration table. Original behavior was reviewed against reference C where usable and against generated MIPS for nonmatching or ambiguous references. The evidence column records the method and validation limits for each function. Inventory/build checks do not establish runtime equivalence.', '',
        'The newer upstream commit `b414b88e0e632674e1d9a075bd24bd61fb58803f` adds desktop UI callback execution. It is outside the pinned source baseline and does not explain the earlier missing audio fix.', '',
        '## Function accounting', '']
    for source in sorted({r['source'] for r in manifest['patches']}):
        lines += ['### ' + source, '', '| Replacement | Purpose | Vita disposition and reason | Evidence / limitation | Next action |', '|---|---|---|---|---|']
        for record in manifest['patches']:
            if record['source'] != source:
                continue
            key = source + '::' + record['function']
            line = definitions[key]['line']
            cells = [f"[{record['function']}](../{source}#L{line})", record['purpose'], record['disposition'] + ': ' + record['reason'], record['evidence'], record['next_action']]
            lines.append('| ' + ' | '.join(c.replace('|', '\\|').replace('\n', ' ') for c in cells) + ' |')
        lines.append('')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='validate inventory and source drift (default)')
    parser.add_argument('--objects', action='store_true', help='also check freshly built patches/*.o')
    parser.add_argument('--recompiled', type=Path, help='also check patch registration metadata in this recompiler output directory')
    parser.add_argument('--write-report', action='store_true', help='regenerate the Markdown report from the manifest')
    parser.add_argument('--require-resolved', action='store_true', help='fail when review or missing-port records remain')
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text())
    definitions = check_manifest(manifest, args.objects)
    if args.recompiled:
        check_recompiled(manifest, args.recompiled)
    report = ROOT / 'docs/VITA_UPSTREAM_PATCH_AUDIT.md'
    rendered = render(manifest, definitions)
    if args.write_report:
        report.write_text(rendered)
    elif report.read_text() != rendered:
        raise ValueError('The Markdown report is stale; regenerate it with --write-report')
    counts = Counter(r['disposition'] for r in manifest['patches'])
    print('Patch inventory checked:', len(definitions), 'source definitions;', sum(r['compiled'] for r in manifest['patches']), 'compiled replacements;', dict(sorted(counts.items())))
    if args.require_resolved and (counts['review_required'] or counts['missing']):
        raise ValueError('Patch integration review is incomplete; see the open audit rows')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print('Patch audit:', error, file=sys.stderr)
        raise SystemExit(1)
