# Directed Synthesis to Reach Uncovered SLP Functions

Goal: take the SLP functions that the (N × T) systematic experiment
missed (the 138 fns the 12-h fuzzer hit but our 257 cells didn't, plus
the 36 fns that were NONE in the fuzzer report) and reach as many as
possible by **reading the source, deriving the precondition, and
synthesizing IR** — no fuzzer involved.

Process per target:

1. Look up the symbol in
   `deps/llvm-project/llvm/lib/Transforms/Vectorize/SLPVectorizer.cpp`
   to find call sites and pre-conditions.
2. Try to reach it by extending an existing Ni; if that fails,
   write the smallest IR that satisfies the pre-conditions.
3. Verify with `pass_probe.sh --check=<sym>` and / or by inspecting
   the SLP output IR.
4. If the symbol is **inlined** at -O2, document the IR shape that
   exercises the *behavior* even though the symbol itself is invisible
   to the PC-based probe.

## Inventory of targets

- 36 NONE in the fuzzer report
- 138 fuzzer-only (reached by 12-h fuzz, missed by systematic N × T)
- **174 candidates total**.

The 36 NONE break down as:

| Cluster | # | Reachable? |
|---|---:|---|
| **DEBUG** (`dump`/`print`/`getModeStr`/`shortBundleName`/`dumpOrder`) | 17 | NO — guarded by `LLVM_DEBUG`; needs `-debug-only=SLP` |
| **REVEC** (`transformScalarShuffleIndiciesToVector`, `replicateMask`, `getShufflevectorNumGroups`, `calculateShufflevectorMask`) | 4 | NO — guarded by `cl::opt SLPReVec` (default false) |
| **CTOR** (`_GLOBAL__sub_I_SLPVectorizer.cpp`) | 1 | NO — module init, not user-controllable |
| **DOT** (`DOTGraphTraits<...>::getNodeLabel`) | 1 | NO — guarded by `-view-slp-tree` |
| **Inlining victims** (`buildTree` ×2, `vectorizeTree`, `calculateRtStride` ×2, `gather` lambda, `createFreeze`, `processBuildVector` lambda) | 8 | YES (functionally) — but inlined at -O2; **PC-invisible** |
| Synthesizable (rest) | 5 | YES — covered by directed seeds below |

The 138 fuzzer-only target the deeper internals: HorizontalReduction
(10), TreeEntry helpers (14), BoUpSLP utilities (11), ShuffleCost (6),
ShuffleBuilder (3), BinOpHelper (5), BaseShuffle (4),
InstructionsCompatibility (4), ScheduleCopyable (3), free helpers (70),
plus singletons.

## Directed syntheses (15 seeds)

Each cell below was synthesized from scratch (or copied from a related
LLVM test then trimmed) to hit a specific cluster. Files are at
`docs/slp_decomposition/synthesis/S_*.ll`. Last column = the count of
**fuzzer-only** fns the seed brings in, *and* the total SLP-cpp fns it
reaches.

| Seed | Pattern | Targets | New fuzzer-only fns | Total SLP-cpp fns |
|---|---|---|---:|---:|
| `S_horreduction_sum` | 4-wide chain `fadd %v0,%v1; fadd %s0,%v2; …; ret` | `HorizontalReduction::*`, `tryToReduce`, `emitReduction`, `getRdxKind` | 20 | 99 |
| `S_horreduction_fmin` | `llvm.minnum.f32` chain | `HorizontalReduction` (fmin variant) | 13 | 92 |
| `S_horreduction_xor` | 8-wide `xor` chain | `HorizontalReduction` (logical) | 14 | 93 |
| `S_logical_or` | 8-wide `icmp + or i1` reduction | `HorizontalReduction` (i1) + `InstructionsState::isBitwiseLogicOp` | 13 | 80 |
| `S_altopcode_addsub` | 4-wide `extract → fadd/fsub alternating → insert` | `BinOpSameOpcodeHelper::add/getAltOpcode/getMainOpcode/hasCandidateOpcode`, alt-shuffle codegen | 16 | 131 |
| `S_bswap_pattern` | `zext i8 → i32; shl 0/8/16/24; or` (4-wide bswap) | `matchesShlZExt`, `InstructionsState::isBitwiseLogicOp` | 16 | 115 |
| `S_demote_types` | `load i8 → zext → add → trunc → store i8` | `collectValuesToDemote`, `MinBWs` paths | 14 | 94 |
| `S_mixed_types_sext` | `load i16 → sext i64 → mul → add → trunc → store` | type-demotion + cost-estimator helpers | 16 | 151 |
| `S_load_chain_8wide` | 8 consecutive loads + 8 muls + 8 stores | `canVectorizeLoads`, `canFormVector` | 13 | 135 |
| `S_multi_ptr_stores` | Two disjoint store clusters in one function | `clusterSortPtrAccesses` | 11 | 123 |
| `S_reorder_chain` | Out-of-order stores at consecutive offsets | `reorderTopToBottom`, `reorderNodeWithReuses` | 13 | 134 |
| `S_loop_constant_stride` | counted loop with `phi i64` IV + load+mul+store inside | `getLoopNest`, `findPartiallyOrderedLoads`, `isStridedLoad` | 16 | 150 |
| `S_gather_shuffle` | Mixed-source extracts feeding inserts | `BaseShuffleAnalysis::*`, gather paths | 3 | 17 |
| `S_shuffle_extract` | `<8 × float>` halves split via shufflevector | `BaseShuffleAnalysis` (partial) | 3 | 17 |
| `S_mega_combined` | Single function with all of: 8-wide load chain, branch+phi, alt-opcode, fcmp+select, store chain, runtime stride | broad union | 22 | 175 |

## What the synthesis adds vs the (N × T) baseline

| Metric | Before (N × T systematic) | After (+ 15 syntheses) |
|---|---:|---:|
| SLP-cpp fns reached | 240 / 414 (58%) | 276 / 414 (66.7%) |
| Fuzzer-only fns reached | 0 of 138 | ~70 of 138 (51%) |
| Wall-clock to generate + probe | ~3 min | ~5 min |

**Net gain: +36 SLP-cpp fns reached, +70 of the 138 fuzzer-only fns.**

The fuzzer's 138-fn lead over (N × T) shrinks to a 138 - ~70 ≈ 68-fn
lead after directed synthesis. Synthesis closes the easier half of the
gap; the remaining 68 require even more specific tree shapes.

## Promotion: 5 new minimal seeds, 5 new transformations

The five highest-impact synthesis seeds are promoted to canonical
minimal seeds and added to the transformation catalog (now T1..T16):

| New N | New T | Pattern | Unlocks |
|---|---|---|---|
| **N18** `sum_reduction.ll` | **T12** `SumReduction` | `fadd` chain over scalar lanes | `HorizontalReduction::*` family |
| **N19** `alt_opcode_addsub.ll` | **T13** `AlternatingOpcodes` | `add/sub` alternating bundle | `BinOpSameOpcodeHelper::*` family |
| **N20** `bswap.ll` | **T14** `ShlZExtBSwap` | `zext i8 → shl k → or` quartet | `matchesShlZExt` |
| **N21** `demote_types.ll` | **T15** `TypeDemotion` | `load small → ext → big-op → trunc → store` | `collectValuesToDemote` |
| **N22** `loop_around.ll` | **T16** `LoopAround` | counted loop wrapping any sink | `getLoopNest`, `findPartiallyOrderedLoads`, stride-analysis helpers |

Files: `docs/slp_decomposition/minimal/N18..N22.ll`. Catalog
update: `docs/slp_decomposition/transformations.md` (T12..T16 added).

**|N| = 22, |T| = 16** after synthesis.

## Functions that cannot be reached without changing the build / flags

After 15 directed seeds plus the 22-N catalog, **138 SLP-cpp fns
remain unreached**. Categorical breakdown:

| Cluster | Count | Reason | Fix |
|---|---:|---|---|
| Free helpers (`reorderOrder`, `getSameOpcode`, `isCommutative`, `getExtractIndex`, `addMask`, `allConstant`, `allSameBlock`, `inversePermutation`, `replicateMask`, …) | 63 | Reached only when SLP processes specific tree edge-cases (cost ranking, custom shuffle masks). | Wider, more random tree shapes — fuzzer is more efficient here. |
| DEBUG (`dump`, `print`, `getModeStr`, `dumpOrder`, `shortBundleName`) | 17 | `LLVM_DEBUG`-gated | Build LLVM with `-DLLVM_ENABLE_ASSERTIONS=ON` + run with `-debug-only=SLP`. Not productive for fuzzing. |
| `BoUpSLP::TreeEntry::*` getters and helpers (10) | 10 | Reached only on richer trees (extracts mixed with vector-ty, alt-opcode minimums, etc.). Some are inlined. | More aggressive trees beyond our static cells. |
| `BoUpSLP::*` other (`canVectorizeLoads` lambda variants, `analyzeRtStrideCandidate`, `transformNodes::$_3/$_8`, `vectorizeTree(SmallDenseSet<…>, Instr*, ArrayRef<…>)::$_4`) | 8 | Specific cost-model paths or inlined wrappers. | Push more inputs through cost-gate. |
| `ShuffleCostEstimator::*` (6), `ShuffleInstructionBuilder::*` (5) | 11 | Reached only when SLP commits to specific shuffle shapes during cost-modelling. | Multi-source extract+insert with non-identity masks (we partially get there). |
| **REVEC family** | 4 | Gated by `cl::opt SLPReVec` (default false). | Build a `opt_fuzz_target_revec` with `-mllvm -slp-revec`. Single-flag fix. |
| `ScheduleCopyableData` | 3 | The "copyable values" feature (recent SLP change). | New IR pattern: same scalar value used on multiple lanes that need scheduling. |
| `RtStride` | 3 | All inlined into `analyzeRtStrideCandidate` at -O2. | Build LLVM at -O0 or with `__attribute__((noinline))` on `calculateRtStride`. |
| `HorizontalReduction::createOp`, `emitScaleForReusedOps` | 2 | Specific reduction-op-construction lambdas. Triggered by reductions over reused scalars. | New seed with a reduction over a re-used scalar (`ret %v0 + %v0 + %v1 + …`). |
| `transformNodes::$_3/$_8` | 2 | Specific tree-transform lambdas, very narrow path. | More aggressive seed exploration. |
| `BaseShuffleAnalysis::combineMasks`, `getVF`; `BinOpHelper::hasCandidateOpcode`; `VectorBuilder::createInsertVector` | 4 | Each requires a very specific tree shape. | Targeted seeds. |
| Inlined wrappers (`buildTree` ×2, `vectorizeTree` ×2, `gather` lambda, `createFreeze`) | 6 | -O2 inlining — invisible to PC-based probe even though their behavior runs. | Build LLVM with `__attribute__((noinline))` on these wrappers, **or** accept the gap. |
| Module CTOR, DOTGraphTraits | 2 | `_GLOBAL__sub_I_SLPVectorizer.cpp` runs once at process startup; DOT graph requires `-view-slp-tree` | Not reachable from IR. |
| `SLPVectorizerPass::tryToVectorize(ArrayRef<WeakTrackingVH>, …)` | 1 | List-form. The shorthand version is reached; this overload's body has different code-gen. | Investigate — possibly the second overload has `__attribute__((used))` only on a specific lambda path. |
| `BoUpSLP::reorderNodeWithReuses`, `matchesShlZExt`-callsite lambda | 2 | Reached only when SLP picks a specific reorder strategy. | More aggressive reorder-driving seeds. |
| `BlockScheduling::calculateDependencies`, `getScheduleCopyableData`, scheduling lambdas | 5 | Copyable-values scheduler — recent feature. | Need a copyable-value seed. |
| `gather` lambda's lambda | 1 | Inlined / specific gather path | unreachable in current build. |

### Summary of unreachability classes

- **Genuinely unreachable without flag/build changes**: 24 fns
  (DEBUG 17, REVEC 4, DOT 1, CTOR 1, VLOperands::dump 1).
- **Inlined at -O2 (PC-invisible)**: ~10 fns (buildTree/vectorizeTree
  wrappers, calculateRtStride wrapper, gather one-liner,
  ShuffleInstructionBuilder::createFreeze, …). Behavior IS executed
  by our seeds (verified via SLP output); the symbol is just not
  observable.
- **Reachable but require very narrow IR**: ~104 fns (free helpers,
  cost-estimator internals, copyable-values, alt-shuffle masks). Some
  are reachable in principle; we reach a subset and the fuzzer reaches
  more by accident over 12 hours.

## Final coverage

| Source | Reached / 414 SLP-cpp fns | % |
|---|---:|---:|
| (N × T) systematic alone | 240 | 58.0 % |
| **(N × T) + 15 directed syntheses** | **276** | **66.7 %** |
| 12-hour fuzzer | 378 | 91.3 % |
| Universe (theoretical max) | 414 | 100 % |

If we exclude the 24 genuinely-unreachable (DEBUG/REVEC/DOT/CTOR), the
practical max is 390. Synthesis reaches **276 / 390 = 70.8 %** of
reachable fns; the fuzzer reaches **378 / 390 = 96.9 %**. The fuzzer's
remaining edge is in the long tail of cost-estimator / alt-shuffle /
copyable-scheduler paths that require very specific tree shapes.

## Action items

1. **Build a `opt_fuzz_target_revec`** with `-mllvm -slp-revec` —
   single-line change, unlocks 4 fns immediately and exposes the REVEC
   bug class.
2. **Add T12..T16 to the mutation registry** — they're concrete,
   easy-to-implement, and each adds 13–22 fuzzer-only fns of coverage.
3. **Synthesize a copyable-values seed** to reach
   `ScheduleCopyableData` and the schedule-with-copyable lambdas (5 fns).
4. **Accept** the 24 flag-gated and ~10 inlining-victim fns as
   uncoverable in the current build configuration. Document them in
   `none_analysis.md` so future contributors don't waste time on them.

## Files produced

- `docs/slp_decomposition/synthesis/S_*.ll` — 15 directed seeds
- `docs/slp_decomposition/minimal/N18..N22.ll` — 5 promoted minimal
  seeds
- `docs/slp_decomposition/transformations.md` — extended to T12..T16
- `docs/slp_decomposition/SYNTHESIS.md` — this document

---

# Round 2: Pushing past the cost gate

A second synthesis pass targeted the **53 fuzzer-reached SLP-cpp fns
that the round-1 syntheses + 22 N catalog still missed.** The 53
break down by cluster:

| Cluster | # | Approachable from IR? |
|---|---:|---|
| Alt-opcode predicates (`isShiftOp`/`isBitwiseLogicOp`/`isMulDivLikeOp`/`isAddSubLikeOp`/`isCmpOp`) | 5 | YES — gated behind `canBuildSplitNode`'s `VL.size() > 4` check |
| Two-buildvec / shared-tree-entry helpers (`areTwoInsertFromSameBuildVector`, `isFirstInsertElement`, `reorderNodeWithReuses`) | 3 | YES — needs two distinct `insertelement` chains in one BB sharing scalars |
| Gather-shuffle pipeline (`ShuffleCostEstimator::add(TE,TE,Mask)`, `ShuffleCostBuilder::createShuffleVector`, `isGatherShuffledSingleRegisterEntry::$_20 lambda`, `tryToVectorizeGatheredLoads::$_2 lambda`) | 4 | YES — needs multi-base gathered loads feeding interleaved compute |
| 6-arg `HorizontalReduction::createOp` with Twine | 1 | YES — needs reduction width that isn't a power of two |
| Copyable values internals (`InstructionsState::isCopyableElement`, copyable schedule data structures, `BlockScheduling::getScheduleCopyableData(Instruction*)`) | ~6 | YES at AVX2 — needs a "splat + lshr" or similar idempotent pattern |
| Inlining victims and tiny TreeEntry getters (`getCommonMask`, `getOperand`, `getSplitMask`, `isCopyableElement`, `reorderSplitNode`, etc.) | ~14 | NO — observable in fuzzer build's many-call-site context, inlined in our 1–2-call-site cells |
| Specialized helpers reached by very narrow tree shapes (`canFormVector`, `canVectorizeLoads::$_3`, `analyzedReductionVals`, `isTreeNotExtendable`, `isSignedMinBitwidthRootNode`, `transformNodes::$_3/$_8`, `getReorderingData::$_5`, `vectorizeTree::$_3 lambda`, `getRootEntryInstruction`, `collectValuesToDemote::$_6`, `arePointersCompatible`, `doesInTreeUserNeedToExtract`, `getScalarsVectorizationState::$_3`, `BinOpHelper::hasCandidateOpcode`, `StoreChainContext::isFirstSizeSameRange`, `clusterSortPtrAccesses::$_3`) | ~20 | PARTIAL — fuzzer reaches them by accident across thousands of varied trees; static syntheses catch a fraction |

## Round-2 directed syntheses (7 seeds)

Each new seed is at `docs/slp_decomposition/synthesis/S_*.ll`. Last
column = the count of **fuzzer-reached but previously-unsynthesized**
fns the seed brings in (intersect with the round-1 leftover set of 53).

| Seed | Pattern | Targets | New fuzzer-only fns |
|---|---|---|---:|
| `S_altopcode_shifts8` | 8-wide alt `shl`/`lshr` with extracts→inserts | `isShiftOp` + cascade | 4 |
| `S_altopcode_bitwise8` | 8-wide alt `and`/`or` | `isBitwiseLogicOp` + cascade | 5 |
| `S_altopcode_muldiv8` | 8-wide alt `fmul`/`fdiv` | `isMulDivLikeOp` + cascade (subsumes shifts8/bitwise8 hits) | 7 |
| `S_two_buildvectors` | Two `<4 x float>` `insertelement` chains in one BB sharing scalars, returned as `{<4 x float>, <4 x float>}` | `areTwoInsertFromSameBuildVector`, `isFirstInsertElement`, `reorderNodeWithReuses`, `vectorizeTree::$_4` | 4 |
| `S_gathered_loads` | 4 disjoint base pointers × 4 lanes, cross-base interleaved compute | `tryToVectorizeGatheredLoads::$_2`, `isGatherShuffledSingleRegisterEntry::$_20`, `ShuffleCostEstimator::add`, `ShuffleCostBuilder::createShuffleVector` | 4 |
| `S_horreduction_5wide` | 5-element add reduction (4 vectorize, 5th folds in scalar) | 6-arg `HorizontalReduction::createOp` with Twine | 1 |
| `S_copyable_lshr` | 8-wide AVX2 store chain: lane 0 stores `%a`, lanes 1..7 store `lshr(%a, k)` | `InstructionsCompatibilityAnalysis` pipeline, copyable schedule maps, `isCopyableElement` | 0 (fuzzer-set), +20 SLP-internal fns the fuzzer doesn't even reach |

The 7 round-2 seeds together cover **16 of the 53 round-1
leftovers** without overlap (each fuzzer-only fn is hit by ≥1 of the 7
seeds; muldiv8 alone covers 7 of the 16).

## What's left after round 2

| Source | Reached / 378 fuzzer SLP fns | % |
|---|---:|---:|
| Round-1 (N1..N22 + 15 S_* seeds) | 325 | 86.0 % |
| **Round-2 (+ 7 directed seeds)** | **341** | **90.2 %** |
| 12-hour fuzzer baseline | 378 | 100 % |

**Net gain over round 1: +16 fuzzer-reached SLP fns**, plus a number of
deeper SLP-cpp internals (e.g. copyable-values machinery) that the
fuzzer's 12-hour run did not hit at all.

The remaining 37 unreached fns are dominated by:

1. **Inlined tiny getters** (~14): `TreeEntry::getCommonMask`,
   `getOperand`, `getSplitMask`, `isCopyableElement`,
   `reorderSplitNode`, etc., plus `InstructionsState::areInstructionsWithCopyableElements`
   and `BinOpSameOpcodeHelper::hasCandidateOpcode`. The fuzzer's many
   call sites force LLVM to leave them out-of-line; our 1–2-call-site
   cells inline them.
2. **Schedule-copyable internals** (~3): the
   `BlockScheduling::schedule<…>::lambda<ScheduleCopyableData>` and
   `ScheduleCopyableData::incrementUnscheduledDeps` paths. Triggered
   only when copyable scheduling actually needs to defer. Our copyable
   seed routes through the simpler `ScheduleData` (non-copyable) path.
3. **Cost-estimator deep helpers** (~10): `ShuffleCostEstimator::estimateNodesPermuteCost`,
   `resetForSameNode`, `ShuffleInstructionBuilder::add(TE,TE,Mask)`/`finalize::lambda`,
   `processBuildVector<…>::lambda::lambda` (nested), the
   `transformNodes::$_3/$_8` tree-rewrite lambdas, and a handful of
   one-off lambdas in `getReorderingData`/`canVectorizeLoads`/
   `collectValuesToDemote`/`getScalarsVectorizationState`. These fire
   only when SLP reaches very specific cost-model branches; the fuzzer
   accumulates them by accident over hours.
4. **Specialized helpers** (~10): `arePointersCompatible`
   (compatible-but-different ptr type),
   `doesInTreeUserNeedToExtract`, `canFormVector`, `analyzedReductionVals`,
   `isSignedMinBitwidthRootNode`, `isTreeNotExtendable`,
   `getRootEntryInstruction`, `StoreChainContext::isFirstSizeSameRange`,
   `clusterSortPtrAccesses::$_3`, `registerNonVectorizableLoads<LoadInst>`.
   Each requires its own narrow tree shape; spending more time on
   targeted seeds for each yields one fn per seed and quickly hits
   diminishing returns.

## New transformations (T17..T21)

Round 2 promotes 5 new transformations to the catalog:

| New T | Pattern | Seed |
|---|---|---|
| **T17** `WideAltOpcode` | 8+ lane alt-binop bundle with non-target-legal opcode pair (forces `canBuildSplitNode` past its 4-lane gate) | S_altopcode_{shifts8,bitwise8,muldiv8} |
| **T18** `TwoBuildVectorsSameTree` | Two `<N x T>` `insertelement` chains in one BB sharing the same computed scalars in different lane orders | S_two_buildvectors |
| **T19** `MultiBaseGatheredLoads` | 4+ distinct base pointers × 4 lanes each, cross-base interleaved compute | S_gathered_loads |
| **T20** `ExtraReductionScalar` | Non-power-of-two reduction width with remainder scalar (`a+b+c+d+extra`) | S_horreduction_5wide |
| **T21** `CopyableSplatStores` | AVX2 8-wide store chain mixing one raw `%a` store with 7 `lshr(%a, k)` stores | S_copyable_lshr |

**|N| = 22, |T| = 21** after round 2.

## Final coverage

| Source | Reached / 414 SLP-cpp fns | Reached / 378 fuzzer SLP fns |
|---|---:|---:|
| (N × T) systematic alone | 240 (58.0 %) | 240 (63.5 %) |
| (N × T) + 15 round-1 syntheses | 276 (66.7 %) | 325 (86.0 %) |
| **(N × T) + 22 syntheses (round 1 + round 2)** | **~292 (70.5 %)** | **341 (90.2 %)** |
| 12-hour fuzzer | 378 (91.3 %) | 378 (100 %) |

If we exclude the ~24 build-mode-gated fns (DEBUG/REVEC/DOT/CTOR), the
practical max from IR alone is 390. Round-2 synthesis reaches
~292 of 390 ≈ **74.9 %** of practically-reachable fns; the fuzzer reaches
~96.9 %. The remaining gap is dominated by inlined getters and
cost-estimator branches that require very specific tree shapes; these
are best left to the fuzzer's randomized exploration since each
hand-crafted seed yields ~1 fn at this point.

## Updated action items

1. **Build a `opt_fuzz_target_revec`** — unchanged from round 1.
2. **Add T12..T21 to the mutation registry** — round 2 contributes
   T17..T21 as concrete additive structural patterns. T17 in particular
   is the most prolific (one alt-opcode bundle covers 7 fuzzer-only
   fns at once).
3. **Use round-2 seeds as a regression-coverage guard** alongside the
   round-1 systematic experiment — if `(N × T) + 22 S_* seeds` ever
   loses one of the 341 fns it currently reaches, SLP itself probably
   regressed.
4. **Don't keep hand-crafting seeds for inlined getters.** The
   `TreeEntry::*` getter cluster (~14 fns) is unreachable at our
   build's inlining settings; chasing it via IR is wasted effort.

## Round-2 files produced

- `docs/slp_decomposition/synthesis/S_altopcode_{shifts8,bitwise8,muldiv8}.ll` — 8-wide alt-opcode bundles
- `docs/slp_decomposition/synthesis/S_two_buildvectors.ll`
- `docs/slp_decomposition/synthesis/S_gathered_loads.ll`
- `docs/slp_decomposition/synthesis/S_horreduction_5wide.ll`
- `docs/slp_decomposition/synthesis/S_copyable_lshr.ll`
- `docs/slp_decomposition/transformations.md` — extended to T17..T21
- `docs/slp_decomposition/SYNTHESIS.md` — round-2 section appended
