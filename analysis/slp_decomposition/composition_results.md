# Phase 3 — Composition Results

We synthesized 11 IR programs that compose T' transformations onto a freshly built base shell. The base shell A (`T_LS4` only) reaches 158 / 416 SLP functions. We measured each composition vs A.

| Cell | Minimal seed (base)                    | Transformation(s) applied | Functions reached (of 416) | New vs bare base | Notes                                                                                       |
| ---- | -------------------------------------- | ------------------------- | -------------------------- | ---------------- | ------------------------------------------------------------------------------------------- |
| H    | (synthetic shell)                      | T_LS2                     | 159                        | —                | width-2 baseline; ~ same as LS4                                                             |
| A    | (synthetic shell)                      | T_LS4                     | 158                        | —                | width-4 baseline                                                                            |
| I    | (synthetic shell)                      | T_LS8                     | 159                        | +1 vs A          | width-8 ≈ width-4 (no real gain)                                                            |
| F    | (synthetic shell)                      | T_LS16 ∘ T_INTR           | 142                        | −16 vs A         | scaling intrinsic to width 16 *loses* coverage (cost-model bails earlier on wide intrinsic) |
| B    | (synthetic shell)                      | T_LS4 ∘ T_INTR            | 157                        | −1 (4 new, 5 lost) | intrinsic ≈ binop for SLP traversal                                                         |
| C    | (synthetic shell)                      | T_LS4 ∘ T_CMPSEL          | 176                        | +18 (26 new, 8 lost) | cmpsel meaningful; activates AltInstr paths                                                 |
| D    | (synthetic shell)                      | T_LS4 ∘ T_COPY            | 215                        | **+57** (58 new, 1 lost) | single copyable lane unlocks the gather/copyable scheduler                                  |
| E    | (synthetic shell)                      | T_LS4 ∘ T_RED             | 124                        | −34 (34 new, 68 lost) | wrapping LS4 in a loop *removes* the trivial-store-chain SLP opportunity                    |
| G    | (synthetic shell)                      | T_LS4 ∘ T_CMPSEL ∘ T_COPY | 157                        | −1 vs A; −58 vs D | non-monotonic — copyable + cmpsel interfere                                                 |
| J    | minimal `horizontal.nsw_propagation_v4i32` (cov 116) | T_COPY                  | 197                        | +81 over base seed | applying T_COPY (udiv lane) to a non-LS4-derived seed: large gain                           |
| K    | (synthetic shell)                      | T_LS4 ∘ T_INTR ∘ T_RED    | **223**                    | +65 over A         | triple composition gives largest gain in the table; **+99 over E (T_LS4∘T_RED), +66 over B (T_LS4∘T_INTR), +74 over A** |

## Pairwise composition (T'_a ∘ T'_b) findings

- **K = T_LS4 ∘ T_INTR ∘ T_RED reaches 223 functions** — neither of T_INTR (157), T_RED (124), nor any pair reaches this alone. Composition unlocks 100 functions over T_LS4∘T_RED alone, including stride / loop / intrinsic-cost-model paths.
- **G = T_LS4 ∘ T_CMPSEL ∘ T_COPY reaches 157** — strictly less than D (T_COPY alone, 215) and less than C (T_CMPSEL alone, 176). The cmpsel and copyable-element analyses *interfere*: when the SLP tree contains both a select-bundle and a copyable lane, the analysis bails earlier than either alone.
- **F = T_LS16 ∘ T_INTR reaches 142** — fewer than narrower B (157). Scaling an intrinsic-bearing program to vector width 16 hits cost-model rejection paths *instead of* the vectorization paths the narrower form reached.

## Ablating width

T_LS{2, 4, 8, 16} bare all reach 158 ± 1 of 416. Width has essentially no effect on traversal coverage of SLP — the same cost-modeling and tree-building paths run regardless of N. Width *does* affect which lowering paths in the backend run, but those are not in `SLPVectorizer.cpp/.h` and so are outside our 416. **Practical implication for fuzzing: do not spend compute generating width variants of an otherwise-identical seed.**

## Applying T' to seeds it was not derived from

- **T_COPY → horizontal.nsw_propagation_v4i32** (cell J): seed went from 116 → 197 (+81). Confirms T_COPY is portable: it unlocks copyable-element paths regardless of the base.
- **T_INTR → bare T_LS4** (cell B): −1 net. T_INTR is *not* portable to an unrelated base; it only "works" when the base provides the right load width.
- **T_RED → bare T_LS4** (cell E): −34. T_RED requires an array stride longer than the LS4 width, otherwise the loop trip count masks the SLP opportunity.

These three results confirm that T' compose non-monotonically and that some T' (like T_RED, T_INTR) require the base to satisfy preconditions to be additive.

## Summary takeaways for fuzzing

1. The most-portable, most-additive transformations are T_COPY (~+50–80) and T_CMPSEL (~+18–26). Apply these aggressively.
2. T_RED should be applied jointly with T_INTR or T_CMPSEL — alone it loses coverage.
3. Width scaling (T_LS_N) is a coverage no-op; pick a single moderate N (e.g., 4 or 8).
4. Triple compositions like T_LS4 ∘ T_INTR ∘ T_RED reach 223 / 416, ~ matching the highest single-seed coverage in the corpus (compare-node-with-reuses at 241).
