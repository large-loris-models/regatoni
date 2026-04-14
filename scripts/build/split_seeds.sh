#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SEEDS_DIR="$PROJECT_ROOT/seeds"
OUT_DIR="$PROJECT_ROOT/seeds_split"

mkdir -p "$OUT_DIR"

python3 - "$SEEDS_DIR" "$OUT_DIR" <<'PY'
import os, re, sys, pathlib

seeds_dir, out_dir = sys.argv[1], sys.argv[2]

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
            # find matching braces
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
            kind = 'other'
            if stripped.startswith('declare'):
                kind = 'declare'
                m = re.search(r'@("[^"]+"|[\w.$-]+)', stripped)
                name = m.group(1).strip('"') if m else ''
                chunks.append((kind, name, line))
            elif stripped.startswith('target '):
                chunks.append(('target', '', line))
            elif stripped.startswith('@'):
                # global
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

IDENT = r'@("[^"]+"|[\w.$-]+)'
ATTR_RE = re.compile(r'(#\d+)')
META_RE = re.compile(r'(![\w.$-]+)')

def referenced_names(text):
    ats = set(m.strip('"') for m in re.findall(IDENT, text))
    attrs = set(ATTR_RE.findall(text))
    metas = set(META_RE.findall(text))
    return ats, attrs, metas

def sanitize(s):
    return re.sub(r'[^\w.-]', '_', s)

created = 0
for path in sorted(pathlib.Path(seeds_dir).glob('*.ll')):
    raw = path.read_text(errors='replace')
    kept = []
    for line in raw.splitlines(keepends=True):
        r = strip_test_comments(line)
        if r is not None:
            kept.append(r)
    text = ''.join(kept)

    try:
        chunks = split_top_level(text)
    except Exception as e:
        print(f'skip {path.name}: {e}', file=sys.stderr)
        continue

    targets = [c for c in chunks if c[0] == 'target']
    globals_ = [c for c in chunks if c[0] == 'global']
    declares = [c for c in chunks if c[0] == 'declare']
    defines  = [c for c in chunks if c[0] == 'define']
    attrs    = [c for c in chunks if c[0] == 'attributes']
    metas    = [c for c in chunks if c[0] == 'metadata']

    for _, fname, body in defines:
        ats, used_attrs, used_metas = referenced_names(body)
        out_parts = []
        out_parts.extend(c[2] for c in targets)
        for c in globals_:
            if c[1] in ats:
                out_parts.append(c[2])
        for c in declares:
            if c[1] in ats:
                out_parts.append(c[2])
        # include other defines' names referenced? we skip — user wants single fn
        out_parts.append(body)
        # attributes referenced
        # re-scan with included pieces
        combined = ''.join(out_parts)
        _, need_attrs, need_metas = referenced_names(combined)
        for c in attrs:
            if c[1] in need_attrs:
                out_parts.append(c[2])
        # metadata: include transitively (simple one-pass)
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

        out_name = f'{path.stem}.{sanitize(fname)}.ll'
        (pathlib.Path(out_dir) / out_name).write_text(''.join(out_parts))
        created += 1

print(f'Created {created} files in {out_dir}')
PY
