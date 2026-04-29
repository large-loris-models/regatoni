# Composition Experiments

For each minimal seed Ni, the composition seeds Ck were hand-built by
applying one or more transformations from `transformations.md` to a base
that did not originally include them. We compare the composition's
coverage against the **union of its ingredient minimal seeds** to detect
emergent (non-additive) behavior.

The five interesting outcomes are:

1. `C == ⋃ ingredients` — strictly additive (no surprise, but confirms the
   transformations are independent and re-usable).
2. `C ⊊ ⋃ ingredients` — substitution lost coverage (e.g. swapping the
   sink from a store chain to a buildvector ret drops the
   store-chain-only functions).
3. `C ⊋ ⋃ ingredients` — composition unlocks **new** functions that
   neither ingredient reaches alone. **Most interesting result.**
4. `C ≠ ⋃ ingredients` — both gain and lose; the transformations
   interact non-trivially.
5. `C == one ingredient` — the transformation didn't take effect (the
   IR ended up structurally equivalent to a single ingredient).

## Results table

| C_id | Composition | Ingredients (Ns) | \|C\| | New vs union | Missing vs union | Outcome |
|---|---|---|---|---|---|---|
| C1 | `T1 ∘ T7` (runtime-stride loads + store chain) | N1, N9 | 17 | — | — | **Additive** |
| C2 | `T1 ∘ T6` (cmp+select pair → store chain) | N1, N12 | 14 | — | all of T6's unlocks | **Substitution failed:** parallel cmp pair didn't form a recognisable cmp cluster |
| C3 | `T2 ∘ T6` (buildvector of cmp+select results) | N4, N12 | 10 | — | all of T6's unlocks | **Substitution failed:** same root cause as C2 |
| C4 | `T1 ∘ T3` (adjacent-lane fsub → store chain) | N1, N2 | 14 | — | T3's HorReduction + N2's buildvector unlocks | **Substitution lost:** HorReduction needs the result to feed an insertelement chain, not stores |
| C5 | `T7 ∘ T2` (runtime-stride loads → buildvector ret) | N4, N9 | 15 | `tryToVectorize`, `vectorizeHorReduction`, `tryToVectorizeList::lambda` | store-chain unlocks | **Emergent:** composition unlocks 3 functions neither ingredient reaches alone |
| C6 | `T6` (3-cmp horizontal-min/max chain) | N12 | 11 | — | `tryToVectorizeList`, `tryToVectorize::lambda`, `tryToVectorizeList::lambda`, `vectorizeCmpInsts::lambda`, `sort_internals` | **Insufficient width:** 3 cmps don't trigger the sorter |
| C7 | `T3` (adjacent-lane fsub on 4 lanes → buildvector) | N2 | 10 | — | `vectorizeHorReduction` | **Pass-1 success blocks pass-2:** at 4-wide the buildvector pass-1 directly handles it, so the HorReduction pass-2 path skips |
| C8 | `T8 ∘ T2` (PHI-fed buildvector) | N4, N5 | 11 | `vectorizeHorReduction` | store-chain unlocks (no stores in C8) | **Emergent:** PHI-fed inserts force pass-1 to fail and pass-2 HorReduction fires |
| C9 | `T6` (4-cmp horizontal-min/max chain) | N12 | 16 | — | — | **Matches N12 exactly** — proves 4 cmps is the minimum width to engage the cmp-cluster sorter |

## What I learned about composition

**Three transformations have hidden context requirements:**

- **T3 (HorReduction)** only fires when the buildvector pass-1 *fails*.
  Whether it fails depends on the lane width, the binop, and the
  specific operand pattern. Adjacent-lane `fsub` works at 2 lanes (N2);
  at 4 lanes the same shape vectorizes directly (C7) and HorReduction
  is skipped. This is a **non-monotone** transformation in its own
  catalog entry.
- **T6 (CmpCluster)** requires ≥4 cmps in a horizontal min/max chain
  (each cmp's result feeds the next via select). Parallel cmps (C2/C3)
  don't engage the cmp sorter, and 3-cmp chains (C6) reach the entry
  but not the sorting lambdas.
- **T8 (BranchPHI)** is *passive* on its own (no new SLP functions),
  but as a wrapper around T2 it forces pass-1 to fail and uncovers
  HorReduction (C8).

**Two compositions emergent:**

- `C5 = T7 ∘ T2`: runtime-strided loads + buildvector return reaches
  `vectorizeHorReduction` because the strided pointer pattern defeats
  pass-1's direct match, leaving pass-2 to run.
- `C8 = T8 ∘ T2`: PHI-fed buildvector — same story, different cause.

**Substitution loses coverage** when a sink-substituting transformation
(buildvector instead of store, or vice-versa) is applied: each sink
reaches its own family of `vectorize{StoreChain,InsertElement}*`
functions. To reach **both** families, the IR must contain *both* a
store chain and a buildvector return — so the maximally-covering seed
will be a deliberate composition, not a single ingredient.

## Net counts

- Compositions tested: **9** (C1–C9)
- Strictly additive: **1** (C1)
- Substitution failures (lost coverage with no gain): **5** (C2, C3, C4, C6, C7)
- Emergent unions (new functions vs ingredients): **2** (C5, C8)
- Exact-match-of-single-ingredient: **1** (C9)
- Functions newly reached by *some* composition that no minimal seed
  reaches alone: **0** — every coverage point reached by C5/C8 is
  already in some Ni's coverage, just not in the specific ingredients
  picked. (E.g. `vectorizeHorReduction` is in N2's coverage; C5/C8
  reaches it through a different path.)

This last point is the punch line: **composition does not unlock SLP
functions that the minimal-seed catalog already misses.** Every NONE
function from `slp_seed_decomposition.md` remains NONE after every
composition I built.

That motivates Phase 4 — finding the **new** transformations needed to
push past the cost gate.
