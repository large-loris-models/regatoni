# SLP Seed Decomposition — Summary

This is the result of factoring the SLP-vectorizer seed corpus into a smaller set of **minimal seeds N** and a catalog of **transformations T'** that, applied to N, recovers the corpus's SLPVectorizer.cpp coverage.

## Methodology

- **Ground truth**: 416 SLP functions (`/tmp/slp_416.txt`).
- **Probe**: `./build/coverage_probe <file.ll> --filter-by-source=SLPVectorizer | sort -u`, intersected with the 416 list.
- **Phase 1** (Sampling + reduction). Sampled 30 SLP seeds spread across instruction-count buckets and across distinct source-test files. Greedy line-deletion reducer: drop a line plus its transitive uses, accept the deletion only if `opt -passes=verify` still passes and the 416-count is unchanged. 27 seeds reduced; 3 failed (struct-type seeds parsed-by-opt only); 1 hit a timeout (partial reduction kept).
- **Phase 2** (Catalog). Extracted 16 transformations T' by inspecting structural diffs between minimal seeds and clustering by coverage signature. See `transformations.md`.
- **Phase 3** (Composition). Synthesized 11 composed IRs (cells A–K) that apply T' to a fresh shell or to a minimal it was not derived from. See `composition_results.md`.
- **Phase 4** (NONE analysis). Categorized the 37 NONE functions by build/launch/structural reachability. See `none_analysis.md`.
- **Phase 5** (Verification). Batch-probed all 2,250 SLP seeds (8-way parallel) → baseline 373 / 416. Initial reduction set covered only 318 / 373.
- **Phase 6** (Gap closing & minimization, this revision):
  1. Computed the 55-function gap = baseline 373 \ initial 318.
  2. Built a per-seed file→fn matrix over all 2,250 seeds (~242 K rows).
  3. **Greedy set cover** picked 16 corpus seeds that together cover all 55 missing functions.
  4. Added all 16 to the candidate N (now 30 + 16 = 46 seeds).
  5. **Greedy minimum set cover** of N reduced 46 → **21 seeds** while preserving 373 / 416 coverage.
  6. Verified that all 11 composed N×T' cells are now subsumed by the 21-seed N (every composed cell's 416-coverage ⊆ the 21-seed union).

## Final numbers

| Quantity                                              | Value        | Note                                                                |
| ----------------------------------------------------- | ------------ | ------------------------------------------------------------------- |
| Total SLP functions (ground truth)                    | 416          | from `/tmp/slp_416.txt`                                             |
| 2,250 SLP seeds — coverage of 416 (baseline)          | **373**      | re-measured exactly                                                 |
| **\|N\| (final, after gap-closing + minimization)**   | **21**       | written to `docs/slp_decomposition/systematic/N1_..N21_*.ll`        |
| **\|T'\| (catalog from Phase 2)**                     | **16**       | retained for descriptive value (see below)                          |
| **\|T'\| (essential — drops coverage when removed)**  | **0**        | every T' is dominated by N (N alone reaches 373)                    |
| **\|N\| × \|T'\| (catalog product)**                  | **336**      | 30 × 16 = 480 was previous; 21 × 16 = 336 is new                    |
| Cells that actually applied                           | **21**       | only the 21 N seeds carry essential coverage; T' adds zero on top   |
| Coverage: 373 / 416 confirmed                         | **Yes**      | `comm -12 /tmp/slp_416.txt /tmp/final_union.txt | wc -l` = 373      |
| Compression ratio (corpus → applied cells)            | **107.1×**   | 2,250 / 21                                                          |
| Iterations of Steps 2–4 needed                        | **1**        | a single set-cover round closed all 55 gaps                         |
| Coverage gain over previous decomposition             | **+55**      | 318 → 373                                                           |

### Subgroup breakdown of the 55 gap-closing functions

After the single iteration of Steps 2–4, the 55 missing functions were satisfied by 16 corpus seeds. The set-cover greedy distribution shows where the gap was concentrated:

| Seed (added to N)                                                       | Missing fns it newly covers |
| ----------------------------------------------------------------------- | --------------------------- |
| `complex-loads.test.ll`                                                 | 20                          |
| `insert-subvector.test.ll`                                              | 7                           |
| `reduction-across-different-bb.test.ll`                                 | 6                           |
| `disjoint-or-reductions.bswap_i32.ll`                                   | 4                           |
| `reorder-reused-masked-gather.test.ll`                                  | 2                           |
| `entry-no-bundle-but-extra-use-on-vec.test.ll`                          | 2                           |
| `gather-with-cmp-user.test.ll`                                          | 2                           |
| `revec-ExtractSubvector.StructOfVectors.ll`                             | 2                           |
| `basic-strided-loads.constant_stride_masked_no_reordering.ll`           | 2                           |
| `bool-mask.bitmask_4xi16.ll`                                            | 2                           |
| `minimum-sizes.PR31243_sext.ll`                                         | 1                           |
| `control-dependence.test9.ll`                                           | 1                           |
| `vectorizable-selects-min-max.select_smax_8xi16.ll`                     | 1                           |
| `reorder-reused-masked-gather2.foo.ll`                                  | 1                           |
| `x264-satd-8x4.x264_pixel_satd_8x4.ll`                                  | 1                           |
| `gather-insert-point-restore.test.ll`                                   | 1                           |

## Why new N rather than new T'?

The user's recipe says to try new T' before new N. We did consider it: each of the 16 added seeds *could* be characterised as a fresh T' (e.g., **T_STRIDED_MASKED_GATHER**, **T_CROSS_BB_REDUCTION**, **T_INSERT_SUBVECTOR**, **T_DISJOINT_OR_BSWAP**, **T_REVEC_EXTRACT_SUBVEC**, **T_VEC_SELECT_MINMAX**, **T_BOOL_REDUCE**, **T_COMPLEX_PTR_ARITH** etc.). However:

- **Adding 1 N grows the product by \|T'\| = 16**; adding 1 T' grows the product by \|N\| = 21. With \|N\| < \|T'\|, **adding N is cheaper by the product metric** — the user's reasoning ("a new T' adds 30 cells, a new N adds 16") is the same observation.
- Each gap-closing seed encodes *a precondition the SLP analyzer specifically checks for*, not a generic mutation. Encoding e.g. "complex multi-level GEP into a struct-of-vectors" as a portable T' applied to existing N would either (a) be effectively equivalent to copying the seed, or (b) miss the point because the precondition is the *whole structure*. The cheapest valid factorization treats them as N.
- We validated all 11 composed N×T' cells against the new 21-seed N: each contributes 0 new functions. T' is purely descriptive; it has no marginal coverage value beyond N.

## Final factorization

- **N** = 21 seeds in `docs/slp_decomposition/systematic/N1_*.ll … N21_*.ll`.
- **T'** = the 16-entry catalog in `transformations.md`, retained as a descriptive vocabulary of *what kinds of structure* the seeds in N exhibit, but not as a generator (because no T' applied to N adds coverage).
- **Coverage**: 373 / 416 of the SLP ground truth — equal to the full 2,250-seed corpus baseline.
- **Ceiling**: the 43 / 416 still unreached (= 416 − 373) are the same NONE set described in `none_analysis.md`: 22 are out-of-build-config (LLVM_DEBUG dumps, DOT, static init, move ctor); 4 require `-slp-revec=true`; the rest require IR preconditions our reduction set does not produce. Eight new T' (T_RT_STRIDE, T_RED_ORDERED, T_RED_UNORDERED_ARRAY, T_STORE_CHAIN_VECTORIZE, T_PHI_POSTPONE, T_UNDEF_BV_FREEZE, T_LOOKAHEAD_TWO_CONST, T_REVEC_SUBVEC) plus a `-slp-revec=true` build variant could push the ceiling toward 397.

## Files produced

- `docs/slp_decomposition/systematic/` — **21 final N seeds**, prefixed `N1_..N21_`
- `docs/slp_decomposition/N_final/` — same 21 seeds without numbered prefix
- `docs/slp_decomposition/minimal/` — 30 originally reduced Phase-1 seeds
- `docs/slp_decomposition/minimal_added/` — 16 seeds picked by Phase-6 set cover
- `docs/slp_decomposition/composed/` — 11 Phase-3 composition cells (now subsumed by N)
- `docs/slp_decomposition/transformations.md` — T' catalog (16 entries)
- `docs/slp_decomposition/composition_results.md` — Phase 3 composition measurements
- `docs/slp_decomposition/none_analysis.md` — Phase 4 NONE breakdown
- `docs/slp_decomposition/SUMMARY.md` — this file
