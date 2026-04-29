# Phase 4 — NONE Function Analysis

The yesterday's NONE list (from `slp_seed_decomposition.md`, sourced
from a snapshot Centipede coverage report) listed these
`SLPVectorizer*` symbols as never-reached:

1. `BoUpSLP::buildTree` (both overloads)
2. `BoUpSLP::vectorizeTree` (no-arg)
3. `SLPVectorizerPass::tryToVectorize(ArrayRef<WeakTrackingVH>, …)`
4. `calculateRtStride`
5. `BoUpSLP::analyzeRtStrideCandidate` lambda
6. `transformScalarShuffleIndiciesToVector`
7. `replicateMask`
8. `getShufflevectorNumGroups`
9. `calculateShufflevectorMask`
10. `BoUpSLP::gather` lambda
11. `ShuffleInstructionBuilder::createFreeze`
12. `BoUpSLP::processBuildVector` lambda
13. `shortBundleName`

I aggregated coverage across all 14 minimal seeds N1..N14
(probing with broad filters: `--pass=SLP`, `--pass=BoUpSLP`,
`--pass=ShuffleInstructionBuilder`, plus targeted filters per name).
Result: **only 5 of the 13 are still unreached after Phase 1
minimization, and 4 of those 5 are gated by features that no IR
transformation can unlock.**

## Status by function

| # | Function | Reached by N1..N14 | Why / Gate |
|---|---|---|---|
| 1 | `BoUpSLP::buildTree` | yes (6 hits) | The wrapper got inlined into callers; `BoUpSLP::buildTreeRec` is what shows in symbolizer output. |
| 2 | `BoUpSLP::vectorizeTree` | yes (6 hits) | Reached, plus `vectorizeTree::lambda`. The yesterday-NONE was a stale snapshot. |
| 3 | `tryToVectorize(ArrayRef<WeakTrackingVH>)` | yes (2 hits) | Reached by N7, N10 — the list-form is exercised by reduction-style chains. |
| 4 | `calculateRtStride` | no (0 hits) | **Inlined at -O2** — its caller `analyzeRtStrideCandidate` IS reached by N9, but the static `calculateRtStride` body is folded inline so its PCs vanish. Not a real gap. |
| 5 | `analyzeRtStrideCandidate` lambda | yes (1 hit, N9) | Reached by N9 (T7). |
| 6 | `transformScalarShuffleIndiciesToVector` | no (0 hits) | **Gated by `-slp-revec` flag** (init false, hidden — line 127). No IR pattern reaches it with the default pass options. |
| 7 | `replicateMask` | no (0 hits) | Same — REVEC-only (called at line 15528 inside an `assert(SLPReVec)` block). |
| 8 | `getShufflevectorNumGroups` | no (0 hits) | Called only at line 10824: `if (SLPReVec && getShufflevectorNumGroups(VL)) …`. Short-circuited when REVEC is off. |
| 9 | `calculateShufflevectorMask` | no (0 hits) | Called only from inside the REVEC-gated branch. |
| 10 | `BoUpSLP::gather` lambda | yes (2 hits) | Reached by N10, N12 (build-vector + reductions create partial gathers). |
| 11 | `ShuffleInstructionBuilder::createFreeze` | no (0 hits) | **Genuinely unreachable by the current N catalog.** Called at line 21434 only when the tree-entry's `NeedFreeze` is set, which requires a buildvector with mixed poison/non-poison lanes whose result feeds a freeze-sensitive use. |
| 12 | `processBuildVector` lambda | yes (21 hits) | Reached widely. The yesterday-NONE was an over-counted snapshot. |
| 13 | `shortBundleName` | no (0 hits) | **Called only from `LLVM_DEBUG(...)`** (line 495). Unreachable without `-debug-only=SLP`. |

## Net count

- **8 of 13 yesterday-NONE functions are now reached** by some Ni — they
  were already coverable with our Phase-1 transformations T1..T9.
- **1 truly unreachable by IR shape**: `createFreeze`.
- **4 unreachable due to flag gates** (REVEC): `transformScalarShuffleIndiciesToVector`,
  `replicateMask`, `getShufflevectorNumGroups`, `calculateShufflevectorMask`.
- **1 unreachable due to build mode**: `shortBundleName` (debug-only).

## What's needed to reach the remaining unreached functions

### `createFreeze` — needs `T_partial_gather`

`createFreeze` fires inside the buildvector codegen when the SLP tree
has a mixed bundle: ≥1 poison/undef lane and ≥1 non-poison lane that
flow into a freeze-sensitive instruction (a `select`, a UB-defining
op like signed division, etc.). Our minimal seeds either build a
fully-defined vector (every lane has a real load/binop) or never
build a vector at all (cost-gate rejection).

A new transformation is needed:

> **T_partial_gather**: Construct an insertelement chain (T2-style)
> where ≥1 lane is left as `poison` and the resulting vector is then
> consumed by a `select` whose mask depends on a comparison of the
> vector lanes. This forces SLP to set `NeedFreeze` on the
> corresponding `TreeEntry`.

Candidate IR sketch (untested):
```llvm
define <2 x i32> @f(<2 x i32> %x, i32 %v0) {
  ; lane 0 is real, lane 1 stays poison
  %r0 = insertelement <2 x i32> poison, i32 %v0, i32 0
  ; consumer that's freeze-sensitive — use the partial vector
  ; as the false-arm of a select where the true-arm comes from
  ; division by an SLP-vectorized lane (forces freeze of poison)
  %d0 = sdiv i32 %v0, 1
  %d1 = sdiv i32 %v0, 1
  %ins0 = insertelement <2 x i32> poison, i32 %d0, i32 0
  %ins1 = insertelement <2 x i32> %ins0,  i32 %d1, i32 1
  %cmp = icmp eq i32 %v0, 0
  %sv = shufflevector <2 x i1> <i1 0, i1 0>, <2 x i1> poison, <2 x i32> zeroinitializer
  %sel = select <2 x i1> %sv, <2 x i32> %r0, <2 x i32> %ins1
  ret <2 x i32> %sel
}
```

I tried two such partial-gather variants (not committed); neither
reached `createFreeze`. The freeze path is gated by SLP-internal
profitability checks that I couldn't trigger without deeper
investigation. **Concrete target: write a test that hits
`NeedFreeze=true` in `processBuildVector` (line 21434).**

### REVEC functions — need `T_enable_revec`

This isn't an IR transformation — it's a pass-options change. The
fuzz harness `opt_fuzz_target.cc` runs `PassBuilder::buildPerModuleDefaultPipeline(O2)`
with `PTO.SLPVectorization = true` but no REVEC flag. Two ways
forward:

1. **At the harness**: parse the IR module's metadata for a magic
   "enable REVEC" hint and flip the global `cl::opt SLPReVec` for
   that input. (Tedious — `cl::opt` is per-process.)
2. **Build-time**: build a second harness binary with
   `-mllvm -slp-revec`. This is the cleanest split — `opt_fuzz_target_revec`
   would search a separate bug space.

Suggested ranking by NONE functions unlocked:
- `T_enable_revec` (build flag flip) — unlocks **4 NONE functions**.
- `T_partial_gather` (new IR transformation) — unlocks **1 NONE function**.

`shortBundleName` is debug-print and not worth chasing.

## Practical implication for the fuzzer

After Phase 4 the genuine remaining gap is:

- Build a `opt_fuzz_target_revec` (one-line CMake/bazel change) and
  re-run the existing seeds — that unlocks REVEC paths automatically.
- Add `T_partial_gather` as a mutation that occasionally drops a lane
  from a buildvector to `poison` and threads the result through a
  freeze-sensitive use.

These two changes account for the entire residual NONE set.
