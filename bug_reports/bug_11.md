Title: [RISCV][GlobalISel] Miscompilation where converting a pointer to a 32-bit integer and back returns the original 64-bit pointer


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - <Compiler Explorer link>
2. GlobalISel - <Compiler Explorer link>

**Test Commit**
[e633c7da8ff144a11b3d875956fbaea06b969070](https://github.com/llvm/llvm-project/commit/e633c7da8ff144a11b3d875956fbaea06b969070)

**Description**
`f` converts a pointer to `i32` (which keeps only the low 32 bits) and back to a pointer (which zero-extends them), so the result is the pointer with its upper 32 bits cleared, but GlobalISel's pre-legalizer combine rule `p2i_to_i2p` folds `G_INTTOPTR (G_PTRTOINT x)` to `x` without checking that the integer in between is as wide as the pointer, so the truncation is dropped and `f` returns its argument unchanged; SelectionDAG keeps it (`slli`/`srli` by 32). Found as written in `llvm/test/Transforms/InstCombine/IntPtrCast.ll` (`@test`) compiled for RISC-V under `-global-isel`.

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define ptr @f(ptr %p) {
  %v = ptrtoint ptr %p to i32
  %q = inttoptr i32 %v to ptr
  ret ptr %q
}
```
```
llc -mtriple=riscv64 input.ll   # SelectionDAG
llc -mtriple=riscv64 -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (clears the upper 32 bits)
f:
	slli	a0, a0, 32
	srli	a0, a0, 32
	ret

; GlobalISel (returns the pointer unchanged)
f:
	ret
```
riscv-tv on the GlobalISel output: `ERROR: Value mismatch`, for `%p` at address `0x100800020` the source returns `0x800020` and the target `0x100800020`; the executed object code (Unicorn) returns `0x100800020`. The SelectionDAG output verifies. Same with `-mattr=+rva23u64`.

**CC**
