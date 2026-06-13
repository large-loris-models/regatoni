#!/usr/bin/env python3
"""Regenerate the seed corpus as guaranteed-valid SINGLE-FUNCTION modules using
llvm-extract, then carve out an integer-only subset for backend (RISC-V) isel
translation-validation fuzzing.

WHY THIS EXISTS (vs scripts/build/split_seeds.py)
-------------------------------------------------
split_seeds.py is a brace-counting *text* splitter. Measured on the current
corpus it has two defects that hurt a careful backend-fuzzing campaign:
  1. It silently drops local type defs (`%T = type {...}`) and cross-function
     `@`-references, so ~3% of its output is invalid IR (undefined type / gep
     base must be sized / undefined value) -- pure noise for the oracle, and the
     source of the one parser-abort we had to quarantine.
  2. "single function" isn't guaranteed: a `{`/`}` inside a string, inline asm,
     or metadata desyncs its hand-rolled depth counter and it leaks 2-10
     functions into one file (~0.4% of files).

llvm-extract parses each module with real LLVM and computes the dependency
closure (types, globals, declares), so every output is a *valid* module holding
*exactly one* defined function. Trade-off: it works at module granularity -- a
module that fails to parse loses all its functions -- but 99.8% of seed modules
parse cleanly on current LLVM, so the yield cost is negligible.

OUTPUTS
-------
  single_func_seeds/  every defined function from every parseable seed, one
                      valid single-function .ll each.
  int_func_seeds/     the clean self-contained SCALAR-integer subset: no FP, no
                      pointers/memory, no external/indirect/asm calls (those make
                      backend TV havoc call results to `freeze poison` -> spurious
                      "incorrect"), no exotic constructs (token/EH/vararg/...), no
                      vectors (backend TV can't lower RVV) and no target-specific
                      intrinsics. Generic @llvm.* integer intrinsics are KEPT.
                      This is the pool for RISC-V scalar isel-rule bug hunting --
                      maximizes real-bug signal, minimizes false alarms.

Filenames reuse split_seeds.py's scheme ("<seed-stem>.<func>.ll") so outputs are
directly comparable to the old corpus.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
LLVM_BIN = PROJECT_ROOT / "deps" / "llvm-build-plain" / "bin"
LLVM_AS = LLVM_BIN / "llvm-as"
LLVM_EXTRACT = LLVM_BIN / "llvm-extract"

# Function-name parsing (same identifier grammar split_seeds.py uses).
DEFINE_RE = re.compile(r"^\s*define\b")
NAME_RE = re.compile(r'@("[^"]+"|[\w.$\-]+)')

# A function is NOT integer-only if it mentions FP types or anything implying a
# pointer / memory access (memory drags in alive2's asm memory model -- a major
# false-alarm source for backend TV).
FP_PTR_RE = re.compile(
    r"\b(?:float|double|half|bfloat|fp128|x86_fp80|ppc_fp128"          # FP types
    r"|getelementptr|alloca|inttoptr|ptrtoint|addrspace|blockaddress"  # ptr/mem ops
    r"|load|store|ptr)\b"                                              # ptr/mem kw
    r"|[%\w\]>]\*"                                                     # typed pointer: i32*, %T*, <N x i8>*
)

# Constructs that make a function a poor isel-rule probe. Verified empirically:
# functions with an external call produce spurious "incorrect" verdicts -- backend
# TV havocs the call result/clobbers to `freeze poison`, so source vs target
# disagree even when codegen is fine (28/28 fails in a 200-sample were call-bearing).
# The exotic constructs below mostly land in `error` (Unsupported type: token, etc.).
EXOTIC_RE = re.compile(
    r"\b(?:token|invoke|landingpad|catchswitch|catchpad|cleanuppad|cleanupret"
    r"|catchret|resume|personality|musttail|callbr|indirectbr|va_arg|asm)\b"
    r"|\.\.\."                                                         # varargs
)
# A `call`/`invoke`/`callbr` INSTRUCTION to anything other than an @llvm.* intrinsic
# (external symbol, indirect via register, or inline asm) is excluded; @llvm.*
# integer intrinsics (ctpop/bswap/bitreverse/fshl/uadd.with.overflow/...) are KEPT.
# The regex anchors the opcode at start-of-instruction (line start or after `=`),
# NOT a bare \bcall\b — otherwise a value named `%call` (clang's default for call
# results) would falsely exclude an otherwise-clean function.
CALL_RE = re.compile(
    r"^\s*(?:%[\w.$-]+\s*=\s*)?(?:tail |musttail |notail )?(?:call|invoke|callbr)\s"
)

# Vectors: backend TV on riscv64 can't lower RVV -- fixed/scalable vector types
# yield "Unsupported type: <... x ...>" or assert (vsetvli). The hunt is SCALAR
# (orc.b / Zbb), so drop anything vector. (Array types [N x T] use [] not <>, and
# aren't matched here.)
VECTOR_RE = re.compile(
    r"<\s*(?:vscale\s+x\s+)?\d+\s+x\s+"                                # <4 x i32>, <vscale x 2 x i1>
    r"|\b(?:shufflevector|extractelement|insertelement)\b"
)
# Target-specific intrinsics for other ISAs (or RVV) don't lower on scalar
# riscv64 -> errors. Generic @llvm.* intrinsics are kept by CALL_RE above.
TARGET_INTRIN_RE = re.compile(
    r"@llvm\.(?:x86|aarch64|arm|amdgcn|nvvm|ppc|hexagon|wasm|s390|mips"
    r"|riscv|ve|bpf|spv|loongarch)\."
)


def is_clean_int_func(text):
    if FP_PTR_RE.search(text):
        return False
    if EXOTIC_RE.search(text):
        return False
    if VECTOR_RE.search(text):
        return False
    if TARGET_INTRIN_RE.search(text):
        return False
    for line in text.splitlines():
        if CALL_RE.match(line) and "@llvm." not in line:
            return False
    return True

# Filename shaping -- copied from split_seeds.py so names line up 1:1.
MAX_STEM_LEN = 80
HASH_LEN = 8
MAX_FILENAME_LEN = 200


def sanitize(s):
    return re.sub(r"[^\w.-]", "_", s)


def shorten_fname(fname):
    s = sanitize(fname)
    if len(s) <= MAX_STEM_LEN:
        return s
    digest = hashlib.md5(fname.encode("utf-8")).hexdigest()[:HASH_LEN]
    return f"{s[:MAX_STEM_LEN]}_{digest}"


def build_out_name(stem, fname):
    name = f"{stem}.{shorten_fname(fname)}.ll"
    if len(name) <= MAX_FILENAME_LEN:
        return name
    digest = hashlib.md5(f"{stem}::{fname}".encode("utf-8")).hexdigest()[:HASH_LEN]
    budget = MAX_FILENAME_LEN - len(".ll") - 1 - HASH_LEN - 1
    head = f"{stem}.{shorten_fname(fname)}"[:budget]
    return f"{head}_{digest}.ll"


def list_defined_funcs(text):
    """Names of functions *defined* (not just declared) in the module text."""
    names = []
    for line in text.splitlines():
        if DEFINE_RE.match(line):
            m = NAME_RE.search(line)
            if m:
                names.append(m.group(1).strip('"'))
    return names


def module_parses(path):
    r = subprocess.run([str(LLVM_AS), "-o", os.devnull, str(path)],
                       capture_output=True)
    return r.returncode == 0


def process_seed(args):
    seed_str, single_dir, int_dir = args
    seed = Path(seed_str)
    out = {"single": 0, "int": 0, "extract_fail": 0, "skipped_module": 0,
           "funcs": 0}

    if not module_parses(seed):
        out["skipped_module"] = 1
        return out

    text = seed.read_text(errors="replace")
    names = list_defined_funcs(text)
    out["funcs"] = len(names)

    for name in names:
        out_name = build_out_name(seed.stem, name)
        single_path = Path(single_dir) / out_name
        r = subprocess.run(
            [str(LLVM_EXTRACT), "-func", name, "-S", str(seed),
             "-o", str(single_path)],
            capture_output=True,
        )
        if r.returncode != 0 or not single_path.exists():
            out["extract_fail"] += 1
            continue
        out["single"] += 1
        # llvm-extract output is valid by construction (LLVM wrote it); classify
        # on the *extracted* module (it carries the closed types/declares).
        body = single_path.read_text(errors="replace")
        if is_clean_int_func(body):
            (Path(int_dir) / out_name).write_text(body)
            out["int"] += 1
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seeds", default=str(PROJECT_ROOT / "seeds"))
    ap.add_argument("--single-dir", default=str(PROJECT_ROOT / "single_func_seeds"))
    ap.add_argument("--int-dir", default=str(PROJECT_ROOT / "int_func_seeds"))
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--limit", type=int, default=0,
                    help="process only the first N seeds (for testing)")
    args = ap.parse_args()

    for tool in (LLVM_AS, LLVM_EXTRACT):
        if not tool.exists():
            sys.exit(f"missing tool: {tool}")

    single_dir = Path(args.single_dir)
    int_dir = Path(args.int_dir)
    single_dir.mkdir(parents=True, exist_ok=True)
    int_dir.mkdir(parents=True, exist_ok=True)

    seeds = sorted(Path(args.seeds).glob("*.ll"))
    if args.limit:
        seeds = seeds[: args.limit]
    print(f"seeds: {len(seeds)}  jobs: {args.jobs}")
    print(f"  single -> {single_dir}")
    print(f"  int    -> {int_dir}")

    totals = {"single": 0, "int": 0, "extract_fail": 0, "skipped_module": 0,
              "funcs": 0}
    work = [(str(s), str(single_dir), str(int_dir)) for s in seeds]
    done = 0
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for r in ex.map(process_seed, work, chunksize=8):
            for k in totals:
                totals[k] += r[k]
            done += 1
            if done % 500 == 0:
                print(f"  {done}/{len(seeds)} seeds | "
                      f"single={totals['single']} int={totals['int']}",
                      flush=True)

    print("\n=== done ===")
    print(f"seed modules:        {len(seeds)}")
    print(f"  skipped (unparseable module): {totals['skipped_module']}")
    print(f"defined functions seen:        {totals['funcs']}")
    print(f"single-function modules:       {totals['single']}  -> {single_dir}")
    print(f"  llvm-extract failures:       {totals['extract_fail']}")
    print(f"integer-only subset:           {totals['int']}  -> {int_dir}")


if __name__ == "__main__":
    main()
