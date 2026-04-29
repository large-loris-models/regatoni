# SLP Transformations T

Each transformation is a structural pattern that, when added to a base IR
function, changes the set of `SLPVectorizer*` functions reached by the SLP
pass. Transformations were derived from coverage deltas across the 14
minimal seeds (`docs/slp_decomposition/minimal/N1..N14`).

The "base" coverage is the 8-function floor reached by every minimal seed
(`PassModel::run`, `SLPVectorizerPass::run`, `SLPVectorizerPass::runImpl`,
`SLPVectorizerPass::collectSeedInstructions`,
`SLPVectorizerPass::vectorizeChainsInBlock` (+ its lambda),
`SLPVectorizerPass::vectorizeCmpInsts`,
`SLPVectorizerPass::vectorizeInserts`).

## Transformation table

| T_id | Name | Description | Seeds it appears in | Functions it unlocks |
|---|---|---|---|---|
| **T1** | `StoreChain` | End the function with ≥2 stores whose pointers are stride-1 GEPs of one base — `store v0, p[0]; store v1, p[1]; …`. Operands can be loads, binops, PHIs — anything. | N1, N5, N6, N9, N10 | `vectorizeStoreChain`, `vectorizeStoreChains`, `vectorizeStores`, `vectorizeStoreChains::lambda`, `vectorizeStores::lambda`, `vectorizeStores::sort_internals` |
| **T2** | `BuildVectorRet` | End the function returning a `<N x T>` constructed via a chain of `insertelement` ops, one per lane. | N2, N4, N8, N13, N14 | `tryToVectorizeList`, `vectorizeInsertElementInst` |
| **T3** | `HorReductionChain` | The compute that feeds either a store chain or a buildvector chain operates **across adjacent lanes of one source vector** — e.g. `fsub %a0, %a1` where `%a0,%a1` come from the same `<2 × T>`. Equivalently: pass-1 buildvector matching fails, so pass-2 HorReduction runs. | N2, N7, N12 | `vectorizeHorReduction` |
| **T4** | `RootInstruction` | Produce a "root instruction" the pass picks up via `vectorizeRootInstruction`. In practice this is reached by either (a) an insertelement chain whose pass-1 match fails (same as T3), (b) a PHI that lands a vectorizable result back into a store/buildvector, or (c) a single sink instruction (e.g. a `call`/`shuffle`) whose operands form a parallel chain. | N6, N7, N10, N11, N12, N14 | `tryToVectorize` (single-Inst form), `vectorizeRootInstruction`, `tryToVectorize::lambda` |
| **T5** | `ListCandidates` | Have ≥2 sibling instructions of the same opcode/shape in the block (e.g. multiple inserts, multiple intrinsic calls, multiple cmps) so the list-form `tryToVectorize` gets a list of length >1. | N4, N7, N8, N10, N12, N13 | `tryToVectorizeList::lambda` (in addition to `tryToVectorizeList` from T2) |
| **T6** | `CmpCluster` | ≥2 `fcmp`/`icmp` results in the same block forming a horizontal min/max chain (`%c0 = fcmp ogt a, b; %m0 = select %c0, a, b; %c1 = fcmp ogt %m0, c; %m1 = select %c1, %m0, c; …`). The cmp-vectorizer's `tryToVectorizeSequence` fires its sort comparator. | N12 | `vectorizeCmpInsts::lambda`, `vectorizeCmpInsts::sort_internals` |
| **T7** | `RuntimeStrideGEP` | Index GEPs by a runtime value expressed as `%off = mul nsw i64 %stride, K` with constant `K` and `%stride` a function argument. | N9 | `vectorizeGEPIndices`, `vectorizeGEPIndices::lambda` |
| **T8** | `BranchPHI` | Control-flow merge: branches join into a block whose PHIs select between values from each predecessor, then those PHI values feed into either a T1 (store chain) or T2 (buildvector). | N5, N6, N11 | None on its own; **composes** with T1/T2 to extend their reach (no new SLP functions, but reaches them with PHI-derived operands). |
| **T9** | `RevecVectorOps` | Operands are themselves vectors (`<4 × float>` etc.), feeding `shufflevector`s and vector intrinsics, terminated by vector stores. | N14 | `tryToVectorize`, `vectorizeRootInstruction` (i.e. T4 via the call-as-root path) — **does not** unlock T2 or T5. |
| **T10** | `AggregateBuildValue` | Return a struct (aggregate type) constructed via an `insertvalue` chain — `insertvalue {T0,T1} undef, %v0, 0; insertvalue %_, %v1, 1; ret`. Each field's value comes from a parallel compute chain (loads → fmul → fadd → fptosi → field). | N15 | `vectorizeInsertValueInst` |
| **T11** | `NonUniformStoreChain` | Store chain whose stored values aren't all-distinct (the same SSA value appears at ≥2 offsets) **or** whose offsets aren't strictly monotonic in source order. Forces the store-chain matcher's resolution callback. | N16 | `vectorizeStoreChain::lambda` |
| **T12** | `SumReduction` | Linear chain of associative ops over scalar lanes from a vector source: `%s0 = fadd %v0, %v1; %s1 = fadd %s0, %v2; … ; ret %sn`. Engages `HorizontalReduction::tryToReduce`/`emitReduction` and reaches reduction-emission helpers (`createOp`, `getReductionCost`, `getRdxKind`, `hasRequiredNumberOfUses`, `isVectorizable`). | N18 | `HorizontalReduction::tryToReduce`, `emitReduction` (×2 overloads), `getReductionCost`, `createOp`, `getRdxKind`, `hasRequiredNumberOfUses`, `isVectorizable`, `matchAssociativeReduction` |
| **T13** | `AlternatingOpcodes` | Bundle of N parallel binops where adjacent lanes have **different opcodes** (e.g. `add %a0,%b0; sub %a1,%b1; add %a2,%b2; sub %a3,%b3`) feeding a buildvector. Engages `BinOpSameOpcodeHelper::add`/`getAltOpcode`/`getMainOpcode` and the alt-shuffle codegen path. | N19 | `BinOpSameOpcodeHelper::*` (4), `TreeEntry::getAltOpcode`, `getAltOpcodeMask`, `addMask` |
| **T14** | `ShlZExtBSwap` | Byte-swap-style pattern: ≥2 `zext i8 → i32` chained through `shl` by constants 0/8/16/24, OR-combined. Forces `BoUpSLP::matchesShlZExt`. | N20 | `matchesShlZExt`, `(anonymous namespace)::InstructionsState::isBitwiseLogicOp` |
| **T15** | `TypeDemotion` | Loads of small integer (e.g. `i8`/`i16`), zero/sign-extend to larger (`i32`/`i64`), arithmetic in the larger type, truncate back to the smaller for storage. Engages `BoUpSLP::collectValuesToDemote`, `MinBWs` book-keeping, and `TreeEntry::getMainOp`/`getOpcode` checks for narrowing. | N21 | `collectValuesToDemote`, `MinBWs`-related TreeEntry getters |
| **T16** | `LoopAround` | Wrap any T1/T7/T12 body in a counted loop. The `phi i64` IV + `add+icmp+br` activates SCEV stride analysis (`isStridedLoad`, `analyzeRtStrideCandidate`), `findPartiallyOrderedLoads`, and the `getLoopNest` path. | N22 | `getLoopNest`, `findPartiallyOrderedLoads`, additional stride-analysis helpers |
| **T17** | `WideAltOpcode` | 8-lane bundle with a binop alternating between two **non-target-legal** opcodes (e.g. `fmul/fdiv`, `shl/lshr`, `and/or`). Pushes past `canBuildSplitNode`'s `VL.size() <= 4` early-exit and exercises the `isShiftOp/isBitwiseLogicOp/isMulDivLikeOp/isAddSubLikeOp/isCmpOp` predicate cascade plus the SK_PermuteTwoSrc cost path. | S_altopcode_{shifts8,bitwise8,muldiv8} | `InstructionsState::isShiftOp/isBitwiseLogicOp/isMulDivLikeOp/isAddSubLikeOp/isCmpOp`, `createInsertVector`, `buildTreeRec::lambda` (alt-opcode branch) |
| **T18** | `TwoBuildVectorsSameTree` | Build two distinct `<N x T>` results in the same BB from the **same** computed scalars in different lane orders. Returning both via an aggregate forces SLP to detect that the two `insertelement` chains feed off the same TreeEntry. | S_two_buildvectors | `areTwoInsertFromSameBuildVector`, `isFirstInsertElement`, `reorderNodeWithReuses`, `vectorizeTree::$_4 lambda` (build-vector finalize) |
| **T19** | `MultiBaseGatheredLoads` | 16+ loads spread across **multiple distinct base pointers** (4 bases × 4 lanes), with a cross-base interleaved compute. Each base is a partial gather; SLP runs `tryToVectorizeGatheredLoads` to attempt secondary vectorization, exercising the gather-shuffle/cost-estimator pipeline. | S_gathered_loads | `tryToVectorizeGatheredLoads::$_2 lambda`, `isGatherShuffledSingleRegisterEntry::$_20 lambda`, `ShuffleCostEstimator::add(TreeEntry,TreeEntry,Mask)`, `ShuffleCostBuilder::createShuffleVector` |
| **T20** | `ExtraReductionScalar` | Reduction whose width is **not** a power of two, leaving a remainder scalar that gets folded into the vector reduction via a final scalar `op`. Reaches the 6-arg `HorizontalReduction::createOp(IRBuilderBase&, RecurKind, Value*, Value*, Twine, ReductionOps)` overload (the form that names the result `op.rdx`). | S_horreduction_5wide | `HorizontalReduction::createOp(…, Twine, …)` (6-arg form) |
| **T21** | `CopyableSplatStores` | 8-lane store chain at AVX2 where lane 0 stores the raw scalar `%a` and lanes 1..7 store `lshr(%a, k)`. SLP models the raw store as an idempotent `lshr-by-0`, exercising the `InstructionsCompatibilityAnalysis` copyable-element pipeline (`isCopyableElement`, `getOperands`, copyable schedule maps). Requires `target-features="+avx2"`. | S_copyable_lshr | copyable-element internals (`InstructionsCompatibilityAnalysis::*`, `isCopyableElement`, `convertTo`, `BlockScheduling::getScheduleCopyableData(Instruction*)`, copyable schedule-map data structures) |
| **T22** | `InterchangeableBinopBundle` | 4-lane integer binop bundle where every lane uses an identity constant making the lanes interchangeable to a single opcode (`add %a,0`, `or %b,0`, `xor %c,0`, `mul %d,1`). Forces `BinOpSameOpcodeHelper::add` + `InterchangeableInfo::trySet` cascade. Behavior verified; symbols are inlined into `buildInstructionsState`. | S_R3_interchangeable_binops | (PC-invisible at -O2) `BinOpSameOpcodeHelper::add`, `getMainOpcode`, `getAltOpcode`, `hasAltOp`, `isBinOpWithConstantInt`, `InterchangeableInfo::*` |
| **T23** | `LoopReductionPHI` | counted loop with a 2-incoming-value reduction PHI of the canonical `phi [0, %entry], [%sum.next, %loop]` form, plus a body-side `%sum.next = add %sum, %x`. Triggers the `if (P->getNumIncomingValues() == 2)` branch in `vectorizeChainsInBlock` line 29753 + the secondary-reduction-root pickup. Adds two new `vectorizeChainsInBlock::lambda` PCs. | S_R3_loop_reduction_phi | `vectorizeChainsInBlock::$_0/$_1` lambdas; behaviour-only hit on `getReductionInstr`, `tryGetSecondaryReductionRoot`, `isReductionCandidate` |
| **T24** | `CrossBlockReductionLoads` | reduction whose loaded operands come from two different basic blocks joined by a branch + PHI. Drives `BoUpSLP::isTreeNotExtendable`'s `!allSameBlock(E.Scalars)` branch (line 17358). Behaviour fires; symbol is inlined into `tryToReduce`. | S_R3_crossblock_reduction | (PC-invisible) `isTreeNotExtendable`'s gather-cross-block path |

## Composition observations

Several minimal seeds are clearly compositions of more primitive transforms:

- **N6 = T1 ∘ T8** — store chain with PHI-derived values. Coverage = T1's
  unlocks ∪ T4's unlocks (PHI of two add results acts as the root).
- **N7 = T2 ∘ T3 ∘ T4 ∘ T5** — buildvector chain (T2) + horizontal
  accumulation (T3) + root instruction (T4) + multiple sibling
  candidates (T5). Coverage matches the union.
- **N10 = T1 ∘ T2 ∘ T3 ∘ T4 ∘ T5** — both store chain and buildvector
  paths, plus the root and list-candidate paths. Highest coverage of any
  individually-firing minimal seed.
- **N12 = T2 ∘ T3 ∘ T4 ∘ T5 ∘ T6** — same as N7 plus the cmp-cluster
  (T6) ⇒ adds `vectorizeCmpInsts::lambda` and `sort_internals`.

## Reusability check

All 7 unique-coverage-changing transformations (T1–T7) appear in **at least
one** minimal seed beyond their "introducing" seed, so they are reusable
patterns rather than seed-specific accidents. T8 and T9 are scaffolding —
T8 lifts other transformations onto control-flow, T9 is a substitution of
scalar ops with vector-of-vector ops.

**T10 and T11 were added in the corpus-wide validation pass** (see
`SUMMARY.md` §"Coverage validation across all 2,250 SLP seeds"). The 30-
seed sample missed two structural shapes that appear in the long tail of
the corpus: aggregate (struct) returns built via `insertvalue` chains,
and store chains with duplicated/non-monotonic stored values. Both are
single-Ni transformations today (T10 ⇒ N15, T11 ⇒ N16); they will likely
become reusable as more sample seeds are decomposed.

## Functions still NOT unlocked by any T

After applying every T to every base, these `SLPVectorizer*` symbols remain
unreached (drawn from the morning's `slp_seed_decomposition.md` NONE list):

- `BoUpSLP::buildTree` (both overloads)
- `BoUpSLP::vectorizeTree`
- `SLPVectorizerPass::tryToVectorize(ArrayRef<WeakTrackingVH>, …)` — the
  list form bound through the explicit `WeakTrackingVH` parameter
- `calculateRtStride` — even N9 with T7 doesn't reach this; T7 reaches
  `vectorizeGEPIndices` but stops before SCEV-based stride detection
- REVEC helpers (`transformScalarShuffleIndiciesToVector`, `replicateMask`)
- Shuffle-bundle helpers (`getShufflevectorNumGroups`,
  `calculateShufflevectorMask`)
- `BoUpSLP::gather` lambda, `ShuffleInstructionBuilder::createFreeze`,
  `BoUpSLP::processBuildVector`

These NONE functions all live downstream of the **cost gate** —
`buildTree` is built but `vectorizeTree` only runs when the cost model
returns "profitable". Phase 4 analyses what new transformations would
push minimal seeds across the gate.
