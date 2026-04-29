# SLP Call-Chain Coverage Analysis

Where **function coverage** asks "which SLP functions did our seeds reach?",
**call-chain coverage** asks "which function-to-function transitions did
they exercise?". A k-chain is a length-k window over the dynamic function
transition sequence captured during one run of the O2 pipeline. Function
coverage at the SLPVectorizerPass surface is already saturated by the
(N × T) decomposition — call-chain coverage is the finer metric we use
here to ask whether we exercise every *interaction pattern* between
top-level SLP entry points, not just every top-level fn in isolation.

The dynamic chains are produced by `coverage_probe`'s new
`--call-chains` / `--call-sequence` modes (PC trace harvested via the
existing sancov `trace_cmp/switch/div/gep/pc_indir` hooks, deduplicated
on consecutive PC, then on consecutive function name after symbolisation
and the same name-cleaning the rest of the SLP analysis uses). The
corresponding wrapper modes live in
`scripts/analysis/pass_probe.sh`. See `characterize_chains.sh` for the
batch driver.

## Methodology

- **Filter**: `--pass=slp-vectorizer` ⇒ `--filter=SLPVectorizer`. This
  surface includes every public `SLPVectorizerPass::*` method and the
  top-level lambdas that share their parent's simple name; it does
  *not* include `BoUpSLP::`, `HorizontalReduction::`, anonymous-namespace
  helpers, etc. — the chain coverage measured here is the **dispatch
  surface**, not the deep helpers.
- **Chain length**: k = 3 (default). k = 5 was sampled and gives the
  same qualitative picture — chain growth tracks dynamic sequence
  length, not seed diversity.
- **Inputs probed**: 16 minimal seeds (`N1..N16`), 80 single-T
  systematic cells (5 additive T's × 16 N's, after dropping
  `; SKIPPED:` stubs), 27 directed-synthesis cells (Round-1: 15;
  Round-2: 7; Round-3: 5).
- **Static upper bound**: enumerated via `enumerate_static_chains.py`
  walking `callgraph.json`. Two scopes — `slpvectorizer-pass` (matches
  the dynamic filter) and `all` (every SLP-cpp class).

## 1. Call-chain signatures for each T

For each transformation `T_j`, the **call-chain signature** is
`⋃_i ((N_i, T_j).chains \ N_i.chains)` — the chains that any (base,
transformation) cell adds beyond the base seed alone. This is only
defined for the five T's that have systematic N×T cells (T1, T3, T6,
T7, T11 — the additive T's per `EXPERIMENT.md`):

| T | Name | Signature size | Interaction it exercises |
|---|---|---:|---|
| T1  | `StoreChain`           | 29 chains | `vectorizeStoreChains` ↔ `vectorizeStores` ↔ `vectorizeStoreChain` triangle, plus `vectorizeStores::sort_internals` ↔ `vectorizeStores::lambda` recursion |
| T3  | `HorReductionChain`    | 23 chains | `tryToVectorize` ↔ `vectorizeRootInstruction` ↔ `vectorizeChainsInBlock` cycle (HorReduction reaches root via the second pass) |
| T6  | `CmpCluster`           | 18 chains | `vectorizeCmpInsts` ↔ `vectorizeCmpInsts::lambda` ↔ `vectorizeCmpInsts::sort_internals` (cmp-vectorizer's sort comparator firing) |
| T7  | `RuntimeStrideGEP`     | **40** chains | `vectorizeGEPIndices` ↔ `vectorizeGEPIndices::lambda` ↔ `tryToVectorizeList` (GEP path is the most chain-rich, because it reaches BOTH root and list pickup paths) |
| T11 | `NonUniformStoreChain` | 32 chains | `vectorizeStoreChain` ↔ `vectorizeStores::lambda` ↔ `vectorizeStores::sort_internals`-heavy (the resolution callback fires more sort iterations) |

(Files: `chains/per_T_signature_T{1,3,6,7,11}.chains`.)

The other T's are **introducing-seed-only** in the systematic
experiment — there is no separate `(N_i, T_j)` cell for them, so the
above formula returns nothing. Their chain delta is best read as
"chains in the introducing seed not in any other minimal seed":

| T | Introducing seed | Chains unique to that seed vs the rest of N |
|---|---|---:|
| T2  | N2  | 0  (chains are subsets of N1/N7/N10's broader unions) |
| T4  | N6  | 3  (PHI-as-root paths) |
| T5  | N4  | 0  |
| T8  | N5  | 0  (T8 is scaffolding; carries no new chains by itself) |
| T9  | N14 | 2  (REVEC-style vector-of-vector calls) |
| T10 | N15 | 11 (struct-build via `vectorizeInsertValueInst`) |

T8's zero-delta result is consistent with the existing finding
(`transformations.md`: "T8 is scaffolding") — branches don't change
the SLP transition graph, only what flows through it.

The single most striking entry in `chains/t_uniqueness.tsv` (chains
that appear in exactly one (N_i, T_j) cell and nowhere else, including
no bare N) is the GEP self-cycle:

```
N9  T7  vectorizeGEPIndices::lambda -> SLPVectorizerPass::vectorizeGEPIndices -> vectorizeGEPIndices::lambda
```

That's the runtime-stride sort-comparator fixpoint — only N9 generates
GEPs that survive the lambda's reorder predicate, and only the +T7
overlay forces the lambda to invoke itself recursively from inside
`vectorizeGEPIndices`.

## 2. Coverage ratios

```
                         k=3 chains
  static  (all SLP-cpp)    2 484     ← loose upper bound
  static  (SLPPass scope)     75     ← scope-matched upper bound
  ---
  dynamic union N          109
  dynamic union N×T        115
  dynamic union S          108
  dynamic union (all)      129       ← what the decomposition currently covers
```

| Comparison | Ratio |
|---|---:|
| dynamic-all / static-all (loose) | **129 / 2 484 ≈ 5.2 %** |
| dynamic-all / static-SLPPass (strict) | 129 / 75 = **172 %** |
| dynamic-N×T / dynamic-N (T contribution) | 115 / 109 = +5.5 % |
| dynamic-S / dynamic-N (synthesis contribution) | 108 / 109 ≈ same |
| synthesis-only chains (S \ N) | 19 |

The strict-scope ratio is **above 100 %** because dynamic chains can
contain *filter-induced false adjacencies*: when execution leaves the
SLPVectorizer surface for `BoUpSLP::*` or anonymous-namespace helpers
and returns, the filter creates a virtual edge from the last
SLPVectorizer-side fn to the next one, which the static graph (built
purely from textual call sites in SLPVectorizer.cpp) does not contain.
The static SLPVectorizerPass-scope number is therefore a strict-graph
*upper bound on direct edges* but a *lower bound on dynamic
chains* — they're complementary, not nested.

The static-all 2 484 number is a defensible loose upper bound because
it includes every chain through every helper class, regardless of
whether it could ever fire under O2. **5.2 % is the headline coverage
ratio**.

## 3. Uncovered chains: clustered, not scattered

64 of the 75 static-SLPPass-scope chains are *not* in the dynamic
union. The distribution of source/middle nodes for those 64 is:

| First fn of uncovered chain | # uncovered chains |
|---|---:|
| `vectorizeChainsInBlock::lambda` | 15 |
| `SLPVectorizerPass::runImpl` | 10 |
| `SLPVectorizerPass::vectorizeChainsInBlock` | 9 |
| `SLPVectorizerPass::vectorizeInserts` | 7 |
| `SLPVectorizerPass::vectorizeCmpInsts` | 7 |
| `SLPVectorizerPass::run` | 6 |

| Middle fn of uncovered chain | # uncovered chains |
|---|---:|
| `vectorizeChainsInBlock::lambda` | 10 |
| `SLPVectorizerPass::vectorizeRootInstruction` | 8 |
| `SLPVectorizerPass::vectorizeInserts` | 8 |
| `SLPVectorizerPass::tryToVectorizeList` | 6 |

Two clear hubs:

1. **`vectorizeChainsInBlock::lambda`** dominates both ends. The
   block-level dispatcher has six static neighbours
   (`vectorizeCmpInsts`, `vectorizeInserts`, `tryToVectorizeList`,
   `tryToVectorize`, `vectorizeRootInstruction`, `vectorizeHorReduction`)
   but our seeds drive only a small number of those edge pairs. Most
   uncovered chains pass through this dispatcher in combinations our
   T's don't construct (e.g.
   `vectorizeChainsInBlock::lambda → vectorizeCmpInsts → vectorizeRootInstruction`,
   `vectorizeChainsInBlock::lambda → vectorizeInserts → vectorizeHorReduction`,
   `vectorizeChainsInBlock::lambda → vectorizeInserts → vectorizeInsertValueInst`).
2. **`vectorizeRootInstruction`** as the middle node. Per
   `transformations.md`, T4 ("RootInstruction") is supposed to drive
   this path but only via the (a)/(b)/(c) sub-shapes that the existing
   minimal seeds happen to materialise — six other in-out pairs are
   feasible per the static graph and unreached.

The clustering is real. Uncovered chains pile up around three
dispatch hubs (`vectorizeChainsInBlock` and its lambda,
`vectorizeRootInstruction`, `vectorizeInserts`) which means the gap is
**not a long tail of unrelated misses but a small number of
multi-output dispatchers we never push through their full fan-out**.
This suggests a "T_dispatcher_combo" transformation — a seed that
forces `vectorizeChainsInBlock::lambda` to take each of its six
out-edges in succession — would close most of the 64-chain gap in a
single shot. Compare with the scattered case (which we don't see):
that would have looked like one or two chains per source, spread
across all 47 SLP-pass nodes evenly.

## 4. Call-chain coverage vs function coverage: the deep gap

`EXPERIMENT.md` reported 138 SLP functions reached by 12 h of fuzzing
but missed by the 257-cell systematic experiment. Reproduced here on
the simplifier-normalised side:

- 272 SLP functions reached by the fuzz run (FULL ∪ PARTIAL, after
  symbol normalisation).
- 25 distinct function nodes appear in our dynamic chain union
  (the SLPVectorizerPass dispatch surface — exactly the surface the
  filter is set to).
- **252** fuzzer-reached fns are *not* nodes in any of our dynamic
  chains. Of those 252:
    - **0** are at the `SLPVectorizerPass::*` surface — every public
      SLP entry point is already a chain node we cover.
    - 34 are top-level lambdas (`parent::lambda` form) of helpers
      we don't filter on.
    - 155 are `BoUpSLP::*` (deep tree-build / cost / scheduling).
    - 63 are anonymous-namespace helpers and the `HorizontalReduction`
      / `BinOpSameOpcodeHelper` / `InstructionsCompatibilityAnalysis`
      families.

**The fn gap lives entirely below the dispatch surface**. Call-chain
coverage at the SLPVectorizer filter is **structurally indifferent**
to those 252 missed fns: they are not chain nodes at this scope, so
neither hitting them nor missing them changes our chain count.

This is the validation the question asks for. Function coverage and
call-chain coverage are measuring different layers:

| Metric | What it sees | What it misses |
|---|---|---|
| Function coverage | Every fn the seed touches anywhere | How those fns connect; the dispatch combinatorics |
| Call-chain coverage at top surface | Every (caller, callee) pair we drive among the public SLP methods | The deep fns invoked from inside each method |

Both are needed. Function coverage caught the 138-fn deep gap the
fuzzer found — that's the layer where directed synthesis (Round-2 and
Round-3 S\_\* seeds) lives. Call-chain coverage at the SLPVectorizer
surface caught a different gap: 64 dispatcher fan-outs we never drive
even though each of the involved fns is reached. **Call-chain
coverage is therefore not a strict replacement for function
coverage — it's a complement**, and the two metrics are largely
orthogonal at the scope we measured.

For a single-metric "is it better?" answer: **for understanding the
gap between systematic and fuzz coverage, function coverage is the
right metric** — chain coverage at the filtered surface can't see
that gap. Chain coverage becomes the right metric when looking at
*how richly we exercise each public SLP method*, where it surfaces
specific dispatcher combinations (the
`vectorizeChainsInBlock::lambda → vectorizeInserts → vectorizeHorReduction`
class of misses) that function coverage hides because every
participating fn is individually reached.

A future probe with a broader filter (e.g. `--filter=SLP` or no
filter, capturing chains across `SLPVectorizerPass::` ↔ `BoUpSLP::` ↔
`HorizontalReduction::`) would test the natural follow-up: does
chain coverage at the *deep* surface track the 138-fn fuzzer-only
gap? The infrastructure is now in place to answer that.

## 5. Files produced

```
docs/slp_decomposition/
├── characterize_chains.sh                    # batch driver
└── chains/
    ├── N{1..16}_*.chains                     # bare minimal seed chains
    ├── N<i>_T<j>.chains                      # systematic single-T cells
    ├── S_*.chains                            # synthesis seeds
    ├── union_N.chains                        # 109 chains
    ├── union_NT.chains                       # 115 chains
    ├── union_S.chains                        # 108 chains
    ├── union_dynamic.chains                  # 129 chains (all dynamic)
    ├── per_T_signature_T{1,3,6,7,11}.chains  # T's chain delta
    ├── t_uniqueness.tsv                      # one-of-a-kind (N,T) chains
    ├── summary.tsv                           # per-file chain counts
    ├── static_k3_slppass.txt                 # 75 static chains, scoped
    ├── static_k3_all.txt                     # 2484 static chains, all SLP-cpp
    └── uncovered_static_slppass.chains       # 64 uncovered (the gap)
scripts/analysis/
├── enumerate_static_chains.py                # static call-chain enumerator
└── pass_probe.sh                             # extended with chain modes
src/tools/
└── coverage_probe.cc                         # ordered-trace + chain output
```

Rerun: `K=3 ./docs/slp_decomposition/characterize_chains.sh`. Set
`JOBS=<n>` for parallelism; `OUT=<dir>` to redirect output.
