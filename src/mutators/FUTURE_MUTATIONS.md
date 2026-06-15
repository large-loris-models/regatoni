# Mutation catalog — RISC-V scalar-integer isel fuzzing

The mutator (`src/mutators/`) is a pluggable set of in-place IR transforms. Each is a
`Mutation` (`base.h`): `name()`, `canApply(M)`, `apply(M, rng)`, registered in `registry.cc`,
built into the fuzz targets via `scripts/build/link_fuzz_target.sh` and tested in
`tests/test_mutations.cc` (run with `scripts/run/run_tests.sh`).

Guiding principle: **isel and the GISel/SelectionDAG combiners pattern-match on constants,
type widths, and small op-trees. Bugs hide at the boundaries of those patterns.** Mutations
that *manufacture* those triggers (constants, idioms, illegal widths) find more than ones that
only reshuffle existing structure. Every mutation must keep the module in the clean
scalar-integer class (no FP/ptr/vector/non-`@llvm.` calls) so the backend-tv oracle accepts it.

## Active (2026-06-13)

Structural: `swap_binop`, `swap_cmp_predicate`, `swap_operands`, `replace_operand`,
`shuffle_instructions`, `move_instruction`, `modify_flags`, `resize_type` (now incl.
odd/boundary widths i1/i3/i6/i7/i17/i24/i31/i33/i48/i63/i128 for legalization stress),
`eliminate_undef`, `inline_call`, `remove_void_call`.

Constant/idiom (added this round):
- **`change_constant`** — retarget a binop/icmp/select integer constant to an interesting
  value (0/1/-1/signbit/2^k/2^k±1/byte+SWAR masks/bit-width) or a perturbation (±1, negate,
  bit-flip, byte-rotate). The single highest-leverage mutation; isel is constant-keyed.
- **`mutate_shift_amount`** — drive shl/lshr/ashr/fshl/fshr amounts to boundaries
  {0,1,7,8,15,16,23,24,31,32,bw/2,bw-1,bw,bw+1}; byte multiples trigger orc.b/rev8/bswap.
- **`wrap_bitmanip`** — wrap a value in bitreverse/bswap/ctpop/ctlz/cttz/rotate or
  x&-x / x&(x-1) / (x>>k)^x. Manufactures the Zbb/orc.b/rev8/clz patterns directly.

Pruned (git history preserves them): `mutate_gep` (ptr-only, dead on integer corpus),
`mutate_unary` (FP-only, dead), `modify_attributes` (injected interrupt/CC attrs → backend-tv
false positives, e.g. the `isr_user` cluster).

## Backlog — designed, not yet implemented (ranked by leverage × simplicity)

1. **`arith_identity_substitution`** — swap between semantically-equal forms the combiners
   canonicalize *differently*: `mul x,2^k ↔ shl x,k`, `sub 0,x ↔ mul x,-1`, `add x,x ↔ shl x,1`,
   `xor x,-1 ↔ sub -1,x`. Manufactures dagisel/gisel canonicalization divergence — the exact
   zone the confirmed `mul nuw x,-1` bug lived in. Pairs with `modify_flags`. (~50 lines)
2. **`narrow_then_widen`** — wrap a 64-bit op as `sext(op(trunc a, trunc b))`, or sandwich a
   value in `trunc i64→i32 … sext i32→i64`. Targets RISC-V `*W` selection (addw/subw/sllw/mulw)
   + redundant sign-extension elimination (`SExtWRemoval`, `hasAllWUsers`) — a classic isel
   hotspot where gisel/dagisel diverge. (~40 lines)
3. **`min_max_idiom`** — convert `select (icmp) x,y` ↔ `@llvm.smin/smax/umin/umax`. Zbb has
   native min/max; the select↔minmax canonicalization is bug-prone. (~40 lines)
4. **`demote_intrinsic_to_expansion`** (and reverse) — replace `@llvm.ctpop(x)` with its SWAR
   bit-twiddle expansion, `bswap` with shift/or, etc. — tests whether the backend *re-recognizes*
   the idiom (this IS orc.b/cpop/rev8 selection). High value for the orc.b goal. (~80 lines)
5. **`sext_inreg`** — introduce `(x << k) >> k` (ashr) sign-extend-in-register. RISC-V
   `sext.b/sext.h` (Zbb) or shift pairs; combiners fold these. (~30 lines)
6. **`bit_test_idiom`** — `and (lshr x, c), 1 ↔ icmp` / single-bit set/clear/invert. Zbs
   (bext/bclr/binv/bset). (~40 lines)
7. **`overflow_intrinsic`** — `add/sub/mul` + overflow compare ↔ `@llvm.{u,s}{add,sub,mul}.with.overflow`
   + extractvalue. Dedicated isel sequences + combiners. (~60 lines)
8. **`saturating_arith`** — introduce `@llvm.{u,s}{add,sub}.sat`. (~40 lines)
9. **`select_to_branch` / `branch_to_select`** — RISC-V select lowering (branches vs Zicond
   czero) is bug-prone. Heavier (CFG edit). (~100 lines)
10. **`mul_by_constant_decompose`** — `mul x,C ↔ shift-add chain`. Strength-reduction isel. (~60 lines)
11. **`reassociate`** — regroup `(a+b)+c → a+(b+c)` with flag interaction. (~40 lines)
12. **`pack_bytes`** — Zbkb `pack/packh` byte-assembly idioms `(a & 0xFF) | (b << 8)`. (~40 lines)

## Notes for larger runs
- The registry picks uniformly. Once the constant/idiom mutations prove out, add a `weight`
  field and up-weight `change_constant` / `mutate_shift_amount` / `wrap_bitmanip` / the
  identity-substitution family — they target the bug-rich combiner-soundness class.
- These compound with **GlobalISel coverage steering** (`CODEGEN_GLOBAL_ISEL=1`): they create
  the constant/type/idiom structure gisel's combiners and legalizer act on.
- Crossover (splice a flag-rich Transforms idiom into a CodeGen pattern) remains the future
  unlock for combining the two seed corpora.
