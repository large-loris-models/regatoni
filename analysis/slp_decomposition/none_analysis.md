# Phase 4 — Analysis of the 37 NONE Functions

The "NONE" set is the 37 SLP functions in the 416 ground truth that no input has reached after a 12-hour fuzzer run. We derived it from `build/workdir_04252026/coverage-report-opt_fuzz_target.000000.snapshot_20260426_045143.txt` per the recipe in the task brief, intersected with `/tmp/slp_416.txt`.

## Categorization

| Group                          | Count | Reachable from any IR in our build? | Reason                                                                                                  |
| ------------------------------ | ----- | ----------------------------------- | ------------------------------------------------------------------------------------------------------- |
| `LLVM_DUMP_METHOD` dump/print  | 19    | **No**                              | Body wrapped in `LLVM_DEBUG(...)`, which compiles to no-op in `-DNDEBUG` builds (our case, see below).  |
| Static initializer             | 1     | No                                  | `_GLOBAL__sub_I_SLPVectorizer.cpp` runs at process startup, before the probe begins recording PCs.       |
| DOTGraphTraits node-label      | 1     | No                                  | `llvm::DOTGraphTraits<BoUpSLP*>::getNodeLabel` is only invoked by `--view-slp-tree` (graphviz dump path). |
| Move constructor               | 1     | No                                  | `SLPVectorizerPass::SLPVectorizerPass(SLPVectorizerPass&&)` — never produced by the pipeline manager in our invocation. |
| RT-stride / strided pointer    | 4     | Yes (with right IR)                 | Need loads/stores whose addresses, expressed in SCEV, decompose into `BasePtr + ConstantOffset` with multiple distinct offsets at one stride. |
| REVEC shufflevector            | 4     | Yes only with `-slp-revec=true`     | All gated behind `if (SLPReVec && getShufflevectorNumGroups(VL))`. The REVEC mode is hidden / off by default. |
| Public `BoUpSLP::buildTree`     | 2     | Yes                                 | Reached from `vectorizeStores` (`Chain` path, line 25611) and the reduction loop (`buildTree(VL)` / `buildTree(VL, IgnoreList)` at 27684/27686). |
| `vectorizeTree()` no-args + `$_1` lambda | 2 | Yes                       | Invoked from `vectorizeStores` and `vectorizeRootInstruction`. The `$_1` lambda is inside the recursive variant. |
| `tryToVectorize(ArrayRef<WeakTrackingVH>, BoUpSLP&)` | 1 | Yes                | Called from `vectorizeChainsInBlock` lines 29116/29464 with `PostponedInsts`. Needs an actual vectorization-postponed instruction in the BB. |
| `gather(ArrayRef<Value*>, ...)` lambda specialization | 1 | Yes              | A specific specialization of the gather template, only chosen when SLP needs to construct a non-vectorized buildvector with the function-ref-based gather sink. |
| `LookAheadHeuristics::getShallowScore` lambda | 1 | Yes                       | Internal lambda in look-ahead score; runs when scoring two values with isolated scoring path. |
| `ShuffleInstructionBuilder::createFreeze` | 1 | Yes                          | Called from `createBuildVector` when `NeedFreeze=true`, which happens when a buildvector contains undef and there is an `ExtractShuffles`/`GatherShuffles` (line 21188). |
| `processBuildVector::lambda` specialization | 1 | Yes                       | A specific template specialization of `processBuildVector`, picked when calling with `ShuffleInstructionBuilder` and the inner lambda overload. |
| `replicateMask` / `shortBundleName` / `transformScalarShuffleIndiciesToVector` | 3 | Yes | All called from buildvector / shuffle-mask paths, mostly under SLPReVec or specific tree shapes. |
| **Total**                      | **37**| —                                   |                                                                                                         |

## Per-group IR preconditions

### Group A: Build-config / launch-config NONE (22 functions — un-fuzzable)

These cannot be reached from any IR with our current binary configuration:

- **19 `LLVM_DUMP_METHOD` symbols** (16 dump + 3 print/printMode/getModeStr + dumpOrder + dumpVectorizableTree + dumpTreeCosts). Compiler.h:`LLVM_DUMP_METHOD` resolves to `LLVM_ATTRIBUTE_NOINLINE` only under `-DNDEBUG` without `LLVM_ENABLE_DUMP` (CMake cache shows `LLVM_ENABLE_DUMP:BOOL=OFF`). The function bodies are still compiled (kept by `-NOINLINE`), but every caller is wrapped in `LLVM_DEBUG(...)`, which expands to a no-op when `NDEBUG` is set. **No IR can reach these without a debug build.**
- **1 DOTGraphTraits**: only invoked by `--view-slp-tree` / `-dot-callgraph` style CLI flags, not our default O2 pipeline.
- **1 `_GLOBAL__sub_I_SLPVectorizer.cpp`**: static initializer runs before `LLVMFuzzerTestOneInput` is called; our PC tracer cannot record it.
- **1 `SLPVectorizerPass(SLPVectorizerPass&&)`**: pass-manager move; not exercised by `runPassPipeline` in `opt`.

**Recommendation**: rebuild a *single* fuzz binary with `-DLLVM_ENABLE_DUMP=ON` and `-debug-only=SLP` instrumentation enabled at probe time, OR mark these 22 as out-of-scope for the 416 ground truth and use `416 − 22 = 394` as the achievable target.

### Group B: REVEC-only functions (4 — need `-slp-revec=true`)

`getShufflevectorNumGroups`, `calculateShufflevectorMask`, `replicateMask`, `transformScalarShuffleIndiciesToVector` are all gated by the `SLPReVec` flag (defined `cl::init(false)` at line 127). In our default `opt -passes=slp-vectorizer` invocation `SLPReVec=false`, so the `if (SLPReVec && ...)` checks short-circuit before these functions are called. The IR pattern would be: a `<NxT>` vector built from `<MxT>` shufflevector subvectors with extract-subvector masks, e.g.

```llvm
%s1 = shufflevector <8 x i32> %v, <8 x i32> poison, <4 x i32> <i32 0,1,2,3>
%s2 = shufflevector <8 x i32> %v, <8 x i32> poison, <4 x i32> <i32 4,5,6,7>
%add = add <4 x i32> %s1, %s2
```

**Recommendation**: add a `-slp-revec=true` (or randomly toggle it) variant of the harness; existing T_BV ∘ T_SHF seeds will then reach these.

### Group C: Reachable but missing the right structural precondition (11 functions)

These require specific IR shapes that none of our 30 minimals or 11 composed seeds happen to produce.

| Function                                                      | IR precondition (new T' needed)                                                                                                        |
| ------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `calculateRtStride` + its `$_0` cmp-lambda                    | T_RT_STRIDE: 4+ load/store pointers expressed as `Base + (i*stride)` where `i` is a loop-induction variable so SCEV decomposes them as `SCEVAddExpr(Constant + AddRec)`. Crucially the constant offsets between adjacent pointers must be **non-power-of-2 and non-element-size**, forcing the runtime stride path instead of the constant stride path. |
| `BoUpSLP::analyzeRtStrideCandidate` `$_0` lambda              | Same as above; the lambda runs once per offset bucket, so we need ≥ 2 distinct constant offsets within the pointer set. |
| `BoUpSLP::buildTree(ArrayRef<Value*>)` (single-arg)           | T_REDUCTION_ORDERED: a horizontal reduction whose `RK == ReductionOrdering::Ordered` (line 27684), e.g., a non-associative `fadd` chain over a vector load (`@llvm.vector.reduce.fadd` with no `reassoc` flag). |
| `BoUpSLP::buildTree(ArrayRef<Value*>, SmallDenseSet&)`        | T_REDUCTION_UNORDERED: an `fadd fast`-style reduction (line 27686 — passes IgnoreList). Our `horizontal.add_red` minimal *almost* hits this but apparently the SLP path used was the chain variant. |
| `BoUpSLP::vectorizeTree()` (no-args)                          | A successful `vectorizeStoreChain` (line 25647) — i.e., 4+ adjacent `store` instructions whose stored values form a vectorizable chain. T_LS4 + actual vectorization must succeed. |
| `BoUpSLP::vectorizeTree(TreeEntry*)::$_1`                     | A tree where vectorizeTree recursion enters a `SplitVectorize` or `StridedLoad` entry; needs T_RT_STRIDE composed with T_RED. |
| `SLPVectorizerPass::tryToVectorize(ArrayRef<WeakTrackingVH>, BoUpSLP&)` | A BB that creates `PostponedInsts` (line 29116) — happens when an `Instruction` is initially deferred (e.g., a `phi` or unhandled opcode) and re-tried at end of `vectorizeChainsInBlock`. Need a phi-using-extractelement (or similar) to populate the postpone list. |
| `gather(...)::$_0::lambda(Value*, Value*)`                    | T_GATHER_FN_REF: a gather operation called via the `function_ref` overload (line 3959). Triggered from `processBuildVector` via the `ShuffleInstructionBuilder` path *with* a non-poison filler. |
| `LookAheadHeuristics::getShallowScore(...)::lambda`           | T_LOOKAHEAD: SLP's reordering invokes look-ahead with two non-instruction values (e.g., constants vs constants, or a load vs a constant). Reached only when SLP decides between two operand orderings during reorder. |
| `ShuffleInstructionBuilder::createFreeze`                     | T_UNDEF_BV_FREEZE: a buildvector containing `undef` lanes *combined with* an extract-shuffle source, e.g., `insertelement %v, undef, i` where the extracted source has poison/undef and the shuffle path picks `NeedFreeze=true` (line 21188). |
| `processBuildVector(ShuffleInstructionBuilder, ...)::lambda::$lambda(Value*)` | The vectorized-buildvector path that calls the inner lambda `(Value*)`. Triggered when buildvector inputs include a chain whose codegen path takes the `ShuffleInstructionBuilder + gather + CreateInsertVector` slow path. |
| `shortBundleName`                                             | Reached only inside `LLVM_DEBUG(... << shortBundleName(...) << ...)` — same `LLVM_DEBUG` story as group A, **belongs in Group A**. |

## What new T' do we need?

| New T'              | Description                                                                                                                                    | Targets which NONE                                                                                                |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| **T_RT_STRIDE**     | inside-loop loads/stores at `gep(p, i*K + j)` for non-power-of-2 K, `j ∈ {0..N-1}`, so SCEV sees `AddRec + ConstantOffset`.                    | `calculateRtStride`, both `$_0` lambdas, `analyzeRtStrideCandidate::$_0`, `vectorizeTree(TreeEntry*)::$_1`         |
| **T_RED_ORDERED**   | non-reassoc `fadd` reduction (no `fast` / `reassoc` flag) over a 4+-wide load.                                                                 | `buildTree(ArrayRef)` single-arg                                                                                   |
| **T_RED_UNORDERED_ARRAY** | same but with `fast`/`reassoc`, run over array slice (the IgnoreList path).                                                              | `buildTree(ArrayRef, SmallDenseSet)`                                                                              |
| **T_STORE_CHAIN_VECTORIZE** | adjacent stores where the *stored values* form a vectorizable tree of binary ops on adjacent loads (forces the `vectorizeStoreChain` body to call vectorizeTree no-arg). | `vectorizeTree()` (no-args) and `tryToVectorize(ArrayRef<WeakTrackingVH>, BoUpSLP&)` (via the postponed re-entry). |
| **T_PHI_POSTPONE**  | a phi that uses extractelement of a vector that itself is built from the loop body — forces SLP to defer and re-try the phi.                    | `tryToVectorize(ArrayRef<WeakTrackingVH>, BoUpSLP&)`                                                              |
| **T_UNDEF_BV_FREEZE** | buildvector with `undef` lanes mixed with extracted-shuffle lanes (drives `NeedFreeze=true`).                                                | `ShuffleInstructionBuilder::createFreeze`                                                                          |
| **T_LOOKAHEAD_TWO_CONST** | A binop whose two operands are constants where SLP's operand-reordering invokes look-ahead score on (Const, Const).                      | `LookAheadHeuristics::getShallowScore::lambda`                                                                    |
| **T_REVEC_SUBVEC**  | shufflevectors with extract-subvector masks producing same-width subvectors *and* run with `-slp-revec=true`.                                 | `getShufflevectorNumGroups`, `calculateShufflevectorMask`, `replicateMask`, `transformScalarShuffleIndiciesToVector` |

## Realistic upper bound

After excluding Group A (build-config) and Group B (`-slp-revec` flag), the achievable target is:

- 416 − 19 (`LLVM_DEBUG` dump) − 1 (DOT) − 1 (static init) − 1 (move ctor) − 1 (`shortBundleName`, also `LLVM_DEBUG`) = **393**
- With `-slp-revec=true` enabled: **393 + 4 = 397**
- The remaining **11 reachable-but-missing** functions require the new T' above. Most plausible composition: T_LS4 ∘ T_RT_STRIDE ∘ T_RED inside a loop with non-power-of-2 stride, plus a separate T_UNDEF_BV_FREEZE buildvector seed.

A 12-hour fuzz that does not produce these specific shapes will plateau at ~379–393, which matches the observed 379.
