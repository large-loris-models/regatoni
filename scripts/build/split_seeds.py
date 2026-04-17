#!/usr/bin/env python3
"""Split multi-function LLVM IR seed files into single-function files."""
import hashlib
import pathlib
import re
import sys

IDENT = r'@("[^"]+"|[\w.$-]+)'
ATTR_RE = re.compile(r'(#\d+)')
META_RE = re.compile(r'(![\w.$-]+)')

MAX_STEM_LEN = 80
HASH_LEN = 8
MAX_FILENAME_LEN = 200


def strip_test_comments(line):
    s = line.lstrip()
    if s.startswith(';'):
        body = s[1:].lstrip().upper()
        for tag in ('RUN:', 'CHECK', 'XFAIL', 'REQUIRES', 'UNSUPPORTED',
                    'RUN-', 'NOTE:', 'MODIFY', 'PR', 'END.'):
            if body.startswith(tag):
                return None
    return line


def split_top_level(text):
    """Split text into top-level chunks: each define{...}, declare, target,
    global, attributes, metadata, etc. Returns list of (kind, name, text)."""
    lines = text.splitlines(keepends=True)
    chunks = []
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            i += 1
            continue
        if stripped.startswith('define'):
            start = i
            buf = ''
            depth = 0
            started = False
            while i < n:
                buf += lines[i]
                for ch in lines[i]:
                    if ch == '{':
                        depth += 1
                        started = True
                    elif ch == '}':
                        depth -= 1
                i += 1
                if started and depth == 0:
                    break
            m = re.search(r'@("[^"]+"|[\w.$-]+)', buf)
            fname = m.group(1).strip('"') if m else f'anon{start}'
            chunks.append(('define', fname, buf))
        else:
            if stripped.startswith('declare'):
                m = re.search(r'@("[^"]+"|[\w.$-]+)', stripped)
                name = m.group(1).strip('"') if m else ''
                chunks.append(('declare', name, line))
            elif stripped.startswith('target '):
                chunks.append(('target', '', line))
            elif stripped.startswith('@'):
                m = re.match(r'@("[^"]+"|[\w.$-]+)', stripped)
                name = m.group(1).strip('"') if m else ''
                chunks.append(('global', name, line))
            elif stripped.startswith('attributes '):
                m = re.match(r'attributes\s+(#\d+)', stripped)
                chunks.append(('attributes', m.group(1) if m else '', line))
            elif stripped.startswith('!'):
                m = re.match(r'(![\w.$-]+)', stripped)
                chunks.append(('metadata', m.group(1) if m else '', line))
            else:
                chunks.append(('other', '', line))
            i += 1
    return chunks


def referenced_names(text):
    ats = set(m.strip('"') for m in re.findall(IDENT, text))
    attrs = set(ATTR_RE.findall(text))
    metas = set(META_RE.findall(text))
    return ats, attrs, metas


def sanitize(s):
    return re.sub(r'[^\w.-]', '_', s)


def shorten_fname(fname):
    sanitized = sanitize(fname)
    if len(sanitized) <= MAX_STEM_LEN:
        return sanitized
    digest = hashlib.md5(fname.encode('utf-8')).hexdigest()[:HASH_LEN]
    return f'{sanitized[:MAX_STEM_LEN]}_{digest}'


def build_out_name(stem, fname):
    name = f'{stem}.{shorten_fname(fname)}.ll'
    if len(name) <= MAX_FILENAME_LEN:
        return name
    digest = hashlib.md5(f'{stem}::{fname}'.encode('utf-8')).hexdigest()[:HASH_LEN]
    budget = MAX_FILENAME_LEN - len('.ll') - 1 - HASH_LEN - 1
    head = f'{stem}.{shorten_fname(fname)}'[:budget]
    return f'{head}_{digest}.ll'


def process_file(path, out_dir):
    raw = path.read_text(errors='replace')
    kept = [r for r in (strip_test_comments(l) for l in raw.splitlines(keepends=True))
            if r is not None]
    text = ''.join(kept)

    try:
        chunks = split_top_level(text)
    except Exception as e:
        return 0, f'parse error: {e}'

    targets = [c for c in chunks if c[0] == 'target']
    globals_ = [c for c in chunks if c[0] == 'global']
    declares = [c for c in chunks if c[0] == 'declare']
    defines = [c for c in chunks if c[0] == 'define']
    attrs = [c for c in chunks if c[0] == 'attributes']
    metas = [c for c in chunks if c[0] == 'metadata']

    if not defines:
        return 0, 'no define blocks'

    created = 0
    for _, fname, body in defines:
        ats, _, _ = referenced_names(body)
        out_parts = []
        out_parts.extend(c[2] for c in targets)
        for c in globals_:
            if c[1] in ats:
                out_parts.append(c[2])
        for c in declares:
            if c[1] in ats:
                out_parts.append(c[2])
        out_parts.append(body)

        combined = ''.join(out_parts)
        _, need_attrs, need_metas = referenced_names(combined)
        for c in attrs:
            if c[1] in need_attrs:
                out_parts.append(c[2])

        md_map = {c[1]: c[2] for c in metas}
        included_md = set()
        frontier = set(need_metas)
        while frontier:
            nxt = set()
            for name in frontier:
                if name in md_map and name not in included_md:
                    included_md.add(name)
                    _, _, more = referenced_names(md_map[name])
                    nxt |= more
            frontier = nxt - included_md
        for name in sorted(included_md):
            out_parts.append(md_map[name])

        out_name = build_out_name(path.stem, fname)
        (out_dir / out_name).write_text(''.join(out_parts))
        created += 1
    return created, None


def main():
    script_dir = pathlib.Path(__file__).resolve().parent
    project_root = script_dir.parent.parent

    seeds_dir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else project_root / 'seeds'
    out_dir = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else project_root / 'split_seeds'

    out_dir.mkdir(parents=True, exist_ok=True)

    input_files = sorted(seeds_dir.glob('*.ll'))
    total_created = 0
    skipped = []

    for path in input_files:
        created, err = process_file(path, out_dir)
        if err:
            skipped.append((path.name, err))
        total_created += created

    print(f'Processed {len(input_files)} input files from {seeds_dir}')
    print(f'Created {total_created} output files in {out_dir}')
    if skipped:
        print(f'Skipped {len(skipped)} files:')
        for name, reason in skipped:
            print(f'  {name}: {reason}')


if __name__ == '__main__':
    main()
