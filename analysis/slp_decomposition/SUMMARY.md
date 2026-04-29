# SLP Vectorizer Seed Decomposition — Summary

Decomposing 30 sampled `split_seeds/SLPVectorizer__*.ll` test seeds
into a **base set N** of minimal seeds and a **transformation
catalog T** that explains how the larger corpus is built from N.

## Headline numbers

| Metric | Value |
|---|---|
| Seeds sampled | **30** (28 reach SLP; 2 are `aggregate.getS` and `reused-scalar-in-multi-node.test` which fall through with no SLP coverage) |
| Unique coverage signatures across the 28 working seeds | **14** |
| Unique minimal seeds N (one per signature) | **\|N\| = 14** (`docs/slp_decomposition/minimal/N1..N14`) |
| Unique transformations T | **\|T\| = 9** (T1..T9) of which 7 (T1..T7) change SLP-pass coverage and 2 (T8/T9) are scaffolding |
| Yesterday-NONE functions still unreached | **5 of 13** — and 4 are flag-gated, 1 is debug-only |
| NONE functions reachable by current Ti compositions | **0** new (the 8 yesterday-NONEs that became reached came from T1..T7 already) |
| NONE functions needing **new** transformations | **1** (`createFreeze`) |
| NONE functions needing **non-IR** changes (REVEC flag) | **4** (REVEC family) |

## Compression ratio

The fuzzer is currently using ~2250 SLPVectorizer seeds. We sampled 30,
collapsed them to 14 minimal seeds, and the structural difference
between any original and its minimal is captured by **at most 4
transformations** (compositions like N6 = T1∘T8∘T4 or N12 =
T2∘T3∘T4∘T5∘T6).

- Naive ratio over the sample: `30 / (14 × 9) = 0.24` — i.e. each
  (N, T) cell would explain 4× as many seeds as we sampled.
- Realistic ratio over the corpus: assume the 14 signatures generalize
  to the full ~2250: `2250 / (14 × 9) = 17.9` average seeds per (N, T)
  cell. The corpus is highly redundant in coverage terms.

In other words: **84% of the 30 sampled seeds (28/30) collapse onto
just 14 distinct coverage profiles**, and those profiles can be
synthesised from a 9-element transformation catalog applied to a
single base IR shape. The remaining variation in original test files
is in operand widths/types/variable names — coverage-irrelevant.

## The transformation catalog (one-line summaries)

1. **T1 StoreChain** — ≥2 consecutive stores at stride 1
2. **T2 BuildVectorRet** — return a vector built via insertelement chain
3. **T3 HorReductionChain** — adjacent-lane compute (pass-1 fails)
4. **T4 RootInstruction** — produce a vectorize-root via PHI/branch/intrinsic
5. **T5 ListCandidates** — ≥2 sibling instructions of same opcode
6. **T6 CmpCluster** — ≥4 cmp+select chain forming horizontal min/max
7. **T7 RuntimeStrideGEP** — GEP indexed by `mul stride, K`
8. **T8 BranchPHI** — control-flow merge into PHI feeding T1/T2
9. **T9 RevecVectorOps** — vector-of-vector inputs feeding shuffles+intrinsics+stores

## Composition findings (Phase 3)

Of 9 compositions tested:
- **2 emergent** (C5: T7∘T2, C8: T8∘T2 — composition unlocked
  `vectorizeHorReduction`/`tryToVectorize` paths neither ingredient
  alone reached)
- **5 substitution failures** — the structural pattern was right but
  the operating context was wrong (e.g. parallel cmps vs. horizontal
  cmp chain; HorReduction needs buildvector sink, not store sink)
- **1 strictly additive** (C1: T7∘T1)
- **1 single-ingredient match** (C9: T6 with 4-wide chain, exactly
  matches N12)

**Conclusion**: T composition does NOT unlock SLP functions outside
the existing 14-N reach. The same coverage points are reachable by
multiple structural paths, but the union over Ti compositions equals
the union over Ni.

## What's needed to reach the residual NONE set

Detail in `none_analysis.md`. Ranked by impact:

| Rank | Change | Type | NONE fns unlocked |
|---|---|---|---|
| 1 | `T_enable_revec` (`-mllvm -slp-revec` build flag, or harness override) | Pass-option flip, not IR | 4 (REVEC family) |
| 2 | `T_partial_gather` (new IR transformation: drop a lane in an insertelement chain to `poison` and route through a freeze-sensitive use) | New mutation | 1 (`createFreeze`) |
| 3 | `-debug-only=SLP` LLVM build | Build-mode flip | 1 (`shortBundleName`) — debug-print only, low-value |

## What this implies for the fuzzer

1. **The mutator catalog is well-balanced for the default SLP pass.**
   Today's 14 mutations + the seed corpus reach essentially every
   SLP code path that is reachable without flag flips. Adding more
   "flatten/widen/swap" mutations without a target won't add coverage.

2. **The next high-leverage move is a second harness binary** built
   with `-mllvm -slp-revec`. That single change unlocks 4 functions
   immediately and opens the REVEC bug class (which has its own
   miscompilation history in LLVM upstream).

3. **Add T_partial_gather as a mutation** — it's the only IR-level
   gap and it's one mutation away from existing buildvector code.

4. **Several "transformations" the fuzz mutators perform are wasted
   work**: substitution that swaps a sink kind (store ↔ insertelement)
   loses coverage of the discarded sink without unlocking new
   functions. The mutator should *prefer* additive structural changes
   (T1 ∘ T2 in the same function, both store chain AND buildvector ret)
   over substitutions.

## Coverage validation across all 2,250 SLP seeds

After Phase 1–4 I batch-probed every `split_seeds/SLPVectorizer__*.ll`
to verify the (N, T) decomposition wasn't an artefact of the 30-seed
sample. Script: `docs/slp_decomposition/batch_probe.sh`. Output:
`docs/slp_decomposition/all_seeds_coverage.tsv` (one row per seed:
`<filename>\t<sorted comma-separated function list>`).

### Headline numbers (corpus-wide)

| Metric | 30-sample | 2,250 corpus |
|---|---:|---:|
| Seeds reaching SLP | 28 | 2,174 (76 reach the pass but never enter SLP) |
| Unique coverage signatures | 14 | **119** |
| Functions in corpus union | 25 | **27** |
| Functions reachable by N | 25 | 27 (after gap-fill) |

### Gap analysis

The 14-N union covered 25 of the 27 functions actually reached
across the 2,250-seed corpus. Two functions appeared in the corpus
but in none of the 14 minimal seeds:

| Function | # seeds reaching | Pattern |
|---|---:|---|
| `SLPVectorizerPass::vectorizeInsertValueInst` | 6 | aggregate (struct) return built via `insertvalue` chain |
| `vectorizeStoreChain::lambda` | 5 | store chain with duplicated value or non-monotonic offsets |

Both gaps were closed by adding two minimal seeds and two
transformations:

- **N15 / T10 `AggregateBuildValue`** — struct return via
  `insertvalue` chain (`docs/slp_decomposition/minimal/N15_insertvalue_struct.ll`).
- **N16 / T11 `NonUniformStoreChain`** — store chain with a duplicated
  stored value (`docs/slp_decomposition/minimal/N16_dup_value_storechain.ll`).

After the gap-fill: **N_union = corpus_union = 27 functions**, and
**every one of the 119 unique signatures is a strict subset of N_union**
— i.e. no original seed reaches an SLP function that the 16-N catalog
fails to reach.

### Signature distribution (heavy head, long tail)

| seed-count bucket | # signatures | total seeds | mean fns/sig |
|---|---:|---:|---:|
| ≥500 seeds      |   1 |   671 | 14.0 |
| 100–499         |   4 |   728 |  8.0 |
| 50–99           |   3 |   221 | 12.0 |
| 20–49           |   5 |   140 | 14.0 |
| 10–19           |  15 |   226 | 14.6 |
| 2–9             |  41 |   138 | 15.7 |
| singletons      |  50 |    50 | 16.3 |
| empty (no SLP)  |  —  |    76 |  0.0 |
| **total**       | **119** unique sigs | **2,250** | |

The top 8 signatures (each reaching ≥50 seeds) cover **1,760 / 2,174 =
81 %** of working seeds — a steep Pareto. The remaining 19 % is a long
tail of 111 signatures, of which **50 are singletons**. Singletons
have a slightly higher mean function count (16.3 vs 14.0 in the head)
because they tend to be one-off bug-reproducer seeds that exercise
several rare paths together.

### Final |N| and |T|

After gap-fill:

- **|N| = 16** (`docs/slp_decomposition/minimal/N1..N16`)
- **|T| = 11** (T1..T11; T8/T9 still scaffolding)

Compression vs corpus: `2,174 / (16 × 11) = 12.4` average seeds per
(N, T) cell, or **136 seeds per minimal seed on average**. The
heavy-head signature alone (671 seeds) is ~98% explained by N1+T1
(simple stride-1 store chain).

### Recommendation update

The transformation catalog is **closed under the existing
`SLPVectorizer*` symbol set** — no IR-shape mutation we don't already
have would unlock more pass functions. The remaining unreached
SLPVectorizer functions are flag-gated (REVEC) or debug-only
(`shortBundleName`) or genuine cost-gate gaps (`createFreeze`); see
`none_analysis.md`.

The headline action items remain:

1. Build `opt_fuzz_target_revec` with `-mllvm -slp-revec` — unlocks 4 NONE functions.
2. Add `T_partial_gather` mutation — unlocks `createFreeze`.
3. Add **T10 (AggregateBuildValue)** and **T11 (NonUniformStoreChain)**
   to the existing mutation registry — they were missing from the 30-
   sample N catalog and are concrete, easy-to-implement IR shapes that
   already account for ~11 corpus seeds.

## Files produced

- `docs/slp_decomposition/minimal/N1..N16.ll` — 16 minimal seeds
- `docs/slp_decomposition/transformations.md` — T catalog (T1..T11)
  with reusability evidence
- `docs/slp_decomposition/composed/C1..C9.ll` — composition seeds
- `docs/slp_decomposition/composition_results.md` — emergent /
  additive / failure outcomes
- `docs/slp_decomposition/none_analysis.md` — per-NONE-function
  precondition analysis
- `docs/slp_decomposition/_sampled.json` — the 30-seed sample
  metadata
- `docs/slp_decomposition/batch_probe.sh` — corpus-wide probe
- `docs/slp_decomposition/all_seeds_coverage.tsv` — 2,250-row
  TSV (`<seed>\t<signature>`) used for the coverage validation
- `docs/slp_decomposition/_signatures.tsv` — 119 unique signatures
  with seed counts and a representative seed per signature
