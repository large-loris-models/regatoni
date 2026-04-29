#!/usr/bin/env python3
"""Generate the systematic N×T matrix.

Each transformation Tj is implemented as either:
  - ADD: appends a self-contained IR snippet just before every `ret` in the
    seed (uses a globally-named buffer so it doesn't depend on the seed's
    SSA values). Applies to any seed.
  - SINK_REPLACE: replaces the existing return path with a different sink
    (struct return for T10, vector return via insertelement for T2). Only
    applies if the seed has a single trailing `ret` and we can drop its
    return value.
  - SKIP: the transformation isn't naturally additive on top of an
    arbitrary seed (T4 RootInstruction, T8 BranchPHI, T9 Revec, T5
    ListCandidates) — these are best-evaluated as compositions, not
    cells.

Output lives under docs/slp_decomposition/systematic/N<i>_T<j>.ll. Cells
that don't apply emit an empty file with a `; SKIPPED: <reason>` header so
Step 2 can record them deterministically.
"""

import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
N_DIR = ROOT / "docs/slp_decomposition/minimal"
OUT_DIR = ROOT / "docs/slp_decomposition/systematic"
OUT_DIR.mkdir(parents=True, exist_ok=True)


# -- T snippets --------------------------------------------------------------
# Each T_ADD entry is (snippet, globals). snippet uses unique SSA names with
# the "tNN_" prefix so it can't clash with any seed.

T_GLOBALS = {
    "T1":  '@T1_buf = local_unnamed_addr global [8 x i64] zeroinitializer, align 16\n',
    "T3":  '@T3_buf = local_unnamed_addr global [4 x double] zeroinitializer, align 16\n',
    "T6":  '@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16\n',
    "T7":  '@T7_buf = local_unnamed_addr global [128 x i8] zeroinitializer, align 16\n',
    "T11": '@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16\n',
}

T_SNIPPETS = {
    # T1 StoreChain: 2 loads + add + 2 consecutive stores.
    "T1": """  ; ---- T1 StoreChain ----
  %t1_g0 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 0
  %t1_g1 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 1
  %t1_v0 = load i64, ptr %t1_g0, align 16
  %t1_v1 = load i64, ptr %t1_g1, align 8
  %t1_a0 = add i64 %t1_v0, 1
  %t1_a1 = add i64 %t1_v1, 1
  %t1_s0 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 4
  %t1_s1 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 5
  store i64 %t1_a0, ptr %t1_s0, align 16
  store i64 %t1_a1, ptr %t1_s1, align 8
""",
    # T3 HorReductionChain: adjacent-lane fsub feeding insertelement (2-wide
    # → pass-1 fails, pass-2 HorReduction fires).
    "T3": """  ; ---- T3 HorReductionChain ----
  %t3_g0 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 0
  %t3_g1 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 1
  %t3_g2 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 2
  %t3_g3 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 3
  %t3_v0 = load double, ptr %t3_g0, align 16
  %t3_v1 = load double, ptr %t3_g1, align 8
  %t3_v2 = load double, ptr %t3_g2, align 16
  %t3_v3 = load double, ptr %t3_g3, align 8
  %t3_h0 = fsub double %t3_v0, %t3_v1
  %t3_h1 = fsub double %t3_v2, %t3_v3
  %t3_i0 = insertelement <2 x double> poison, double %t3_h0, i32 0
  %t3_i1 = insertelement <2 x double> %t3_i0, double %t3_h1, i32 1
  %t3_so = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 2
  store <2 x double> %t3_i1, ptr %t3_so, align 16
""",
    # T6 CmpCluster: 4-wide horizontal max chain (fcmp+select).
    "T6": """  ; ---- T6 CmpCluster ----
  %t6_g0 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 0
  %t6_g1 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 1
  %t6_g2 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 2
  %t6_g3 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 3
  %t6_v0 = load i32, ptr %t6_g0, align 16
  %t6_v1 = load i32, ptr %t6_g1, align 4
  %t6_v2 = load i32, ptr %t6_g2, align 8
  %t6_v3 = load i32, ptr %t6_g3, align 4
  %t6_c0 = icmp slt i32 %t6_v0, %t6_v1
  %t6_m0 = select i1 %t6_c0, i32 %t6_v0, i32 %t6_v1
  %t6_c1 = icmp slt i32 %t6_m0, %t6_v2
  %t6_m1 = select i1 %t6_c1, i32 %t6_m0, i32 %t6_v2
  %t6_c2 = icmp slt i32 %t6_m1, %t6_v3
  %t6_m2 = select i1 %t6_c2, i32 %t6_m1, i32 %t6_v3
  %t6_so = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 0
  store i32 %t6_m2, ptr %t6_so, align 16
""",
    # T7 RuntimeStrideGEP: pointer GEP indexed by a runtime mul. Uses a
    # dummy runtime stride read from an opaque global to defeat constant
    # folding.
    "T7": """  ; ---- T7 RuntimeStrideGEP ----
  %t7_stride = load volatile i64, ptr @T7_buf, align 8
  %t7_s0 = mul nsw i64 %t7_stride, 0
  %t7_s1 = mul nsw i64 %t7_stride, 1
  %t7_s2 = mul nsw i64 %t7_stride, 2
  %t7_s3 = mul nsw i64 %t7_stride, 3
  %t7_p0 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s0
  %t7_p1 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s1
  %t7_p2 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s2
  %t7_p3 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s3
  %t7_v0 = load i8, ptr %t7_p0, align 1
  %t7_v1 = load i8, ptr %t7_p1, align 1
  %t7_v2 = load i8, ptr %t7_p2, align 1
  %t7_v3 = load i8, ptr %t7_p3, align 1
  %t7_o0 = getelementptr inbounds i8, ptr @T7_buf, i64 100
  %t7_o1 = getelementptr inbounds i8, ptr @T7_buf, i64 101
  %t7_o2 = getelementptr inbounds i8, ptr @T7_buf, i64 102
  %t7_o3 = getelementptr inbounds i8, ptr @T7_buf, i64 103
  store i8 %t7_v0, ptr %t7_o0, align 1
  store i8 %t7_v1, ptr %t7_o1, align 1
  store i8 %t7_v2, ptr %t7_o2, align 1
  store i8 %t7_v3, ptr %t7_o3, align 1
""",
    # T11 NonUniformStoreChain: 4 stores at offsets 0..3 where the same
    # value is stored at offsets 0 and 2.
    "T11": """  ; ---- T11 NonUniformStoreChain ----
  %t11_g0 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 0
  %t11_g1 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 1
  %t11_v0 = load i32, ptr %t11_g0, align 16
  %t11_v1 = load i32, ptr %t11_g1, align 4
  %t11_d0 = udiv i32 %t11_v0, 3
  %t11_d1 = udiv i32 %t11_v1, 8
  %t11_o0 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 0
  %t11_o1 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 1
  %t11_o2 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 2
  %t11_o3 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 3
  store i32 %t11_d0, ptr %t11_o0, align 16
  store i32 %t11_d1, ptr %t11_o1, align 4
  store i32 %t11_d0, ptr %t11_o2, align 8
  store i32 %t11_d1, ptr %t11_o3, align 4
""",
}

# Transformations we can't naturally "add" to an arbitrary seed. We keep them
# in the matrix and emit a SKIPPED stub so Step 2 can count them.
T_SKIP_REASON = {
    "T2":  "BuildVectorRet replaces the seed's return type with a vector — covered by N4/N2/N8 etc., not additive.",
    "T4":  "RootInstruction is an interaction (PHI/branch/intrinsic shape) on the seed's existing values — covered as composition T8∘T2 etc.",
    "T5":  "ListCandidates is implicitly satisfied whenever any other Tj is added (the snippet contains ≥2 sibling sub-expressions).",
    "T8":  "BranchPHI requires re-routing the seed's ret through a new merge BB — not orthogonal to the seed body.",
    "T9":  "RevecVectorOps replaces all scalar inputs with vector inputs — covered by N14, not additive.",
    "T10": "AggregateBuildValue replaces the seed's return type with a struct — covered by N15, not additive.",
}

ALL_T = ["T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "T9", "T10", "T11"]


# -- IR transformation -------------------------------------------------------

# Match `define ... @<name>(...)` (cap end of return type+params) — no body
# rewrite. We just inject globals at the top and snippets before each ret.

RET_RE = re.compile(r"^( *)ret\b", re.MULTILINE)


def apply_add(seed_text: str, t_id: str) -> str:
    glob = T_GLOBALS[t_id]
    snippet = T_SNIPPETS[t_id]
    # Inject the global at the top of the file, after any existing
    # `target ...` directives but before the first `define`.
    lines = seed_text.splitlines(keepends=True)
    insert_at = 0
    for i, ln in enumerate(lines):
        if ln.lstrip().startswith("define "):
            insert_at = i
            break
    out = "".join(lines[:insert_at]) + glob + "".join(lines[insert_at:])
    # Indented snippet inserted before every `ret`.
    def _inj(m):
        return snippet + m.group(0)
    out = RET_RE.sub(_inj, out)
    return out


def emit_skip(t_id: str, reason: str) -> str:
    return f"; SKIPPED: {t_id} not applied additively — {reason}\n"


def main():
    seeds = sorted(N_DIR.glob("N*.ll"),
                   key=lambda p: int(re.match(r"N(\d+)", p.stem).group(1)))
    if not seeds:
        sys.exit("no minimal seeds found")

    n_applied = 0
    n_skipped = 0
    for seed in seeds:
        n_id = re.match(r"(N\d+)", seed.stem).group(1)
        text = seed.read_text()
        for t_id in ALL_T:
            out_path = OUT_DIR / f"{n_id}_{t_id}.ll"
            if t_id in T_SNIPPETS:
                try:
                    out_path.write_text(apply_add(text, t_id))
                    n_applied += 1
                except Exception as e:
                    out_path.write_text(emit_skip(t_id, f"generator error: {e}"))
                    n_skipped += 1
            else:
                out_path.write_text(emit_skip(t_id, T_SKIP_REASON[t_id]))
                n_skipped += 1
    print(f"generated {n_applied} cells, skipped {n_skipped} cells "
          f"(stubs written for skips)")


if __name__ == "__main__":
    main()
