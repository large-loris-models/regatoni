Title: [MemoryBuiltins] Miscompilation where llvm.objectsize with min=true returns 2^64-7 for a select of two pointers when one is past the end of its object


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - <Compiler Explorer link>
2. GlobalISel - <Compiler Explorer link>

**Test Commit**
[e633c7da8ff144a11b3d875956fbaea06b969070](https://github.com/llvm/llvm-project/commit/e633c7da8ff144a11b3d875956fbaea06b969070)

**Description**
`%p` is either 3 bytes into a 4-byte alloca (1 byte left) or 8 bytes into a 1-byte alloca (past the end, 0 bytes left), so `llvm.objectsize(%p, min=true)` must return a lower bound on the bytes left, at most 0. Instead it folds to `-7` (2^64-7). For a select or phi in `Min` mode, `ObjectSizeOffsetVisitor::combineOffsetRange` takes the minimum `Before` (3) and, separately, the minimum `After` (-7, the past-the-end operand). `compute` then returns Size = Before + After = -4 and Offset = 3, and `getSizeWithOverflow` only clamps when `Offset.isNegative() || Size.ult(Offset)`. That comparison is unsigned, so the negative Size passes as a huge value and the result is Size - Offset = -7. With a single past-the-end pointer (no select/phi) the result is correctly 0. Same code on `main`. In `llc` the fold happens in `pre-isel-intrinsic-lowering` (both instruction selectors give `li a0, -7`); `opt -passes=lower-constant-intrinsics` folds it the same way. Found by varying a constant in `llvm/test/Transforms/LowerConstantIntrinsics/builtin-object-size-phi.ll` (`@pick_negative_offset_different_width_index_maybe_too_large`, `%buffer1` size 8 -> 1).

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i64 @f(i1 %c) {
  %a = alloca [4 x i8]
  %b = alloca i8
  %pa = getelementptr i8, ptr %a, i64 3
  %pb = getelementptr i8, ptr %b, i64 8
  %p = select i1 %c, ptr %pa, ptr %pb
  %min = call i64 @llvm.objectsize.i64.p0(ptr %p, i1 true, i1 false, i1 false)
  ret i64 %min
}
```
```
llc -mtriple=riscv64 input.ll   # SelectionDAG
llc -mtriple=riscv64 -global-isel input.ll   # GlobalISel
opt -passes=lower-constant-intrinsics -S input.ll
```

**Output**
```
; SelectionDAG and GlobalISel (identical)
f:
	addi	sp, sp, -16
	li	a0, -7
	addi	sp, sp, 16
	ret

; opt -passes=lower-constant-intrinsics
  ret i64 -7
```
riscv-tv on the `llc` output: `ERROR: Value mismatch`. For `%c = 0` the source returns `0` and the target `0xfffffffffffffff9`. In the original test's variant (a phi in place of the select) the executed object code (Unicorn) also returns `0xfffffffffffffff9` where the source returns 0. Same with `-mattr=+rva23u64`.

**CC**
