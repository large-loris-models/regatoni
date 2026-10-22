Title: [RISCV][GlobalISel] Miscompilation where sign-extending an i16 to i128 and shifting it left by 0 zero-extends instead


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - <Compiler Explorer link>
2. GlobalISel - <Compiler Explorer link>

**Test Commit**
[e633c7da8ff144a11b3d875956fbaea06b969070](https://github.com/llvm/llvm-project/commit/e633c7da8ff144a11b3d875956fbaea06b969070)

**Description**
`shl (sext x), 0` is just `sext x`. GlobalISel's pre-legalizer combine `reduce_shl_of_extend` rewrites it to `zext (shl x, 0)`. `CombinerHelper::matchCombineShlOfExtend` accepts a `G_SEXT` (or `G_ANYEXT`) source as well as a `G_ZEXT`, but `applyCombineShlOfExtend` always builds a `G_ZEXT`. That is only correct when the narrow shift result's sign bit is known zero. The guard `MinLeadingZeros >= ShiftAmt` gives that for a shift of 1 or more, but for a shift of 0 it holds trivially (0 >= 0), so a sign extension becomes a zero extension. For `%b = 0x8000` the source returns `0xffff...ffff8000` and GlobalISel returns `0x8000`.

`mul (sext x), 1` reaches the same rule through `mul_to_shl` (1 = 2^0). On RISC-V it shows up at `i128`; at `i32`/`i64` the shift by 0 is folded first. Same code on `main`.

Found by varying a constant in `llvm/test/CodeGen/AArch64/adc.ll` (`@test_extended`, shift amount 3 -> 0) and `llvm/test/CodeGen/NVPTX/arithmetic-int.ll` (`@smul_lohi_i64`, a multiplier made 1; that variant's `ashr` then became `srli`) compiled for RISC-V under `-global-isel`. Disabling the rule (`-riscvprelegalizercombiner-disable-rule=reduce_shl_of_extend`) fixes both.

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i128 @f(i16 %b) {
  %e = sext i16 %b to i128
  %s = shl i128 %e, 0
  ret i128 %s
}
```
```
llc -mtriple=riscv64 input.ll   # SelectionDAG
llc -mtriple=riscv64 -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (sign-extends: high half is the sign)
f:
	slli	a1, a0, 48
	srai	a0, a1, 48
	srai	a1, a1, 63
	ret

; GlobalISel (zero-extends: high half 0)
f:
	slli	a0, a0, 48
	srli	a0, a0, 48
	li	a1, 0
	ret
```
riscv-tv on the GlobalISel output: `ERROR: Value mismatch`. For `%b = 0x8000` the source returns `0xffffffffffffffffffffffffffff8000` and the target `0x8000`. The SelectionDAG output verifies. With `-mattr=+rva23u64` GlobalISel emits `zext.h a0, a0` / `li a1, 0`. For the `@smul_lohi_i64` variant (`trunc (ashr (mul (sext i64 %a to i128), 1), 1)`), GlobalISel emits `srli a0, a0, 1`, and the executed object code (Unicorn) returns `0x4000000000000000` for `%a = 0x8000000000000000` where the source returns `0xc000000000000000`.

**CC**
