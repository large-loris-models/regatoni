#!/usr/bin/env python3
"""
Normalize LLVM IR files to remove incidental differences:
  - Strip all value names via bitcode round-trip with --discard-value-names
  - Strip all comments, source_filename, uselistorder directives
  - Rename defined function(s) to f, g, h, ...
  - Preserve target datalayout / target triple

Usage:
  normalize_ir.py <input.ll>                # in-place
  normalize_ir.py <input.ll> <output.ll>    # write to output
  normalize_ir.py file1.ll file2.ll ...     # batch in-place (3+ args)

Tool paths are read from $LLVM_AS and $OPT, falling back to
$LLVM_BUILD_PLAIN/bin/{llvm-as,opt}, and finally to a project-relative
default so the script works standalone.
"""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def _resolve_tool(env_name: str, basename: str) -> str:
    val = os.environ.get(env_name)
    if val:
        return val
    plain = os.environ.get("LLVM_BUILD_PLAIN")
    if not plain:
        # Project-relative default: scripts/analysis/normalize_ir.py
        # → project root is parents[2].
        project_root = Path(__file__).resolve().parents[2]
        plain = str(project_root / "deps" / "llvm-build-plain")
    return str(Path(plain) / "bin" / basename)


LLVM_AS = _resolve_tool("LLVM_AS", "llvm-as")
OPT = _resolve_tool("OPT", "opt")

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


def _process_pair(input_path: Path, output_path: Path) -> bool:
    try:
        normalized = normalize(input_path.read_text())
        output_path.write_text(normalized)
        if input_path == output_path:
            print(f'normalized {input_path.name}')
        else:
            print(f'normalized {input_path.name} -> {output_path.name}')
        return True
    except Exception as e:
        print(f'error: {input_path}: {e}', file=sys.stderr)
        return False


def main():
    args = sys.argv[1:]
    if not args:
        print(f'Usage: {sys.argv[0]} <input.ll> [output.ll]', file=sys.stderr)
        print(f'   or: {sys.argv[0]} file1.ll file2.ll ...   (3+ args = batch in-place)',
              file=sys.stderr)
        sys.exit(1)

    if len(args) == 1:
        ok = _process_pair(Path(args[0]), Path(args[0]))
        sys.exit(0 if ok else 1)
    elif len(args) == 2:
        ok = _process_pair(Path(args[0]), Path(args[1]))
        sys.exit(0 if ok else 1)
    else:
        any_failed = False
        for path in args:
            p = Path(path)
            if not _process_pair(p, p):
                any_failed = True
        sys.exit(1 if any_failed else 0)


if __name__ == '__main__':
    main()
