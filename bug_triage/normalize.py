#!/usr/bin/env python3
"""
Normalize LLVM IR files to remove incidental differences:
  - Strip all value names via bitcode round-trip with --discard-value-names
  - Strip all comments, source_filename, uselistorder directives
  - Rename defined function(s) to f, g, h, ...
  - Preserve target datalayout / target triple
"""

import re
import sys
import subprocess
import os
import tempfile
from pathlib import Path

LLVM_AS = os.path.expanduser("~/llvm-project/for-alive/bin/llvm-as")
OPT     = os.path.expanduser("~/llvm-project/for-alive/bin/opt")

IDENT = r"[\w.]+"


def strip_inline_comment(line):
    """Remove trailing '; ...' from a line, respecting quoted strings."""
    in_str = False
    for i, ch in enumerate(line):
        if ch == '"':
            in_str = not in_str
        elif ch == ';' and not in_str:
            return line[:i].rstrip()
    return line


def discard_names(text):
    """
    textual IR → bitcode (llvm-as) → textual IR with all value names
    discarded (opt --discard-value-names).  Returns (ir_text, error_str).
    """
    # source_filename blocks llvm-as when names will be discarded; strip it.
    clean = '\n'.join(l for l in text.splitlines()
                      if not l.strip().startswith('source_filename'))

    with tempfile.NamedTemporaryFile(mode='w', suffix='.ll', delete=False) as f:
        f.write(clean)
        tmp_ll = f.name
    tmp_bc = tmp_ll[:-3] + '.bc'

    try:
        r1 = subprocess.run([LLVM_AS, tmp_ll, '-o', tmp_bc],
                            capture_output=True, text=True)
        if r1.returncode != 0:
            return None, r1.stderr.strip()

        r2 = subprocess.run(
            [OPT, '--discard-value-names', '-S', '--passes=', tmp_bc],
            capture_output=True, text=True)
        if r2.returncode != 0:
            return None, r2.stderr.strip()

        return r2.stdout, None
    finally:
        for p in (tmp_ll, tmp_bc):
            if os.path.exists(p):
                os.unlink(p)


def normalize(text):
    ir, err = discard_names(text)
    if ir is None:
        print(f'  warning: discard_names failed: {err}', file=sys.stderr)
        return text

    # Drop comment lines (including ; ModuleID), source_filename, uselistorder;
    # strip inline comments from everything else.
    filtered = []
    for line in ir.splitlines():
        s = line.strip()
        if (s.startswith(';') or
                s.startswith('source_filename') or
                s.startswith('uselistorder')):
            continue
        filtered.append(strip_inline_comment(line))

    text = '\n'.join(filtered)

    # Strip implicit %N arg labels from define signatures (redundant there).
    def strip_sig_arg_nums(line):
        brace = line.rfind('{')
        paren = line.find('(')
        if paren < 0:
            return line
        inner = line[paren + 1 : brace if brace >= 0 else len(line)]
        inner = re.sub(r'\s*%\d+', '', inner)
        inner = re.sub(r'  +', ' ', inner)
        inner = re.sub(r' ([,)])', r'\1', inner)
        return line[:paren + 1] + inner + (line[brace:] if brace >= 0 else '')

    text = '\n'.join(strip_sig_arg_nums(l) if re.match(r'^define\b', l) else l
                     for l in text.splitlines())

    # Collect global variable definitions: module-level lines of the form
    # "@name = ..." (distinct from declare/define which start with those words).
    global_vars = []
    global_seen = set()
    for line in text.splitlines():
        m = re.match(r'^(@' + IDENT + r')\s*=', line)
        if m:
            name = m.group(1)[1:]  # strip leading @
            if name not in global_seen:
                global_seen.add(name)
                global_vars.append(name)

    global_rename = {old: f'g{i}' for i, old in enumerate(global_vars)}

    # Collect defined functions.
    func_defs = re.findall(r'^define\b.*?@(' + IDENT + r')', text, re.MULTILINE)
    fn_names = list('fghijklmnopqrstuvwxyz') + [f'f{i}' for i in range(100)]
    func_rename = {old: fn_names[i] for i, old in enumerate(func_defs)}

    # Apply all renames simultaneously (longest key first to avoid prefix
    # collisions), so no substitution is re-processed by another.
    all_rename = {**global_rename, **func_rename}
    if all_rename:
        keys = sorted(all_rename, key=len, reverse=True)
        pat = re.compile(r'@(' + '|'.join(re.escape(k) for k in keys) + r')(?![\w.])')
        text = pat.sub(lambda m: '@' + all_rename[m.group(1)], text)

    return text


def main():
    paths = sys.argv[1:]
    if not paths:
        print(f'Usage: {sys.argv[0]} file.ll [...]', file=sys.stderr)
        sys.exit(1)
    for path in paths:
        p = Path(path)
        try:
            p.write_text(normalize(p.read_text()))
            print(f'normalized {p.name}')
        except Exception as e:
            print(f'error: {p}: {e}', file=sys.stderr)


if __name__ == '__main__':
    main()
