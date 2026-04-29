#!/usr/bin/env python3
"""For every Ni and every pair (T_a, T_b) of additive transformations,
build the composed cell N<i>_T<a>_T<b>.ll by injecting both snippets."""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_systematic import (T_GLOBALS, T_SNIPPETS, T_SKIP_REASON, RET_RE)

N_DIR = ROOT / "docs/slp_decomposition/minimal"
OUT_DIR = ROOT / "docs/slp_decomposition/systematic"

ADDABLE = list(T_SNIPPETS.keys())  # T1, T3, T6, T7, T11


def apply_pair(seed_text: str, t_a: str, t_b: str) -> str:
    glob = T_GLOBALS[t_a] + T_GLOBALS[t_b]
    snippet = T_SNIPPETS[t_a] + T_SNIPPETS[t_b]
    lines = seed_text.splitlines(keepends=True)
    insert_at = 0
    for i, ln in enumerate(lines):
        if ln.lstrip().startswith("define "):
            insert_at = i
            break
    out = "".join(lines[:insert_at]) + glob + "".join(lines[insert_at:])
    out = RET_RE.sub(lambda m: snippet + m.group(0), out)
    return out


def main():
    seeds = sorted(N_DIR.glob("N*.ll"),
                   key=lambda p: int(re.match(r"N(\d+)", p.stem).group(1)))
    n = 0
    for seed in seeds:
        n_id = re.match(r"(N\d+)", seed.stem).group(1)
        text = seed.read_text()
        for i, ta in enumerate(ADDABLE):
            for tb in ADDABLE[i + 1:]:
                p = OUT_DIR / f"{n_id}_{ta}_{tb}.ll"
                p.write_text(apply_pair(text, ta, tb))
                n += 1
    print(f"wrote {n} composed cells")


if __name__ == "__main__":
    main()
