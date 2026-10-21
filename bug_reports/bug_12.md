Title: [RISCV][SelectionDAG] Miscompilation where the signed high multiply of an i1 by 1 returns its operand instead of 0


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/TK8obd5oM
2. GlobalISel - https://godbolt.org/z/ej13GKWYM

**Test Commit**
[e633c7da8ff144a11b3d875956fbaea06b969070](https://github.com/llvm/llvm-project/commit/e633c7da8ff144a11b3d875956fbaea06b969070)

**Description**
`llvm.smulh.i1(%x, 1)` is the high bit of the signed 2-bit product of `%x` and `1`, and as a signed `i1` the constant `1` is `-1`, so for `%x = 1` (also `-1`) the product is `1` and its high bit is `0`: the result is `0` for both inputs. `DAGCombiner::visitMULHS` folds `mulhs x, 1` to `sra x, BitWidth - 1` because the `i1` constant is one, which for `i1` is `sra x, 0` = `x`, so `f` returns `%x`; the same fold is only valid when `1` is positive, i.e. for widths above 1. GlobalISel computes the product and returns `0`. (InstCombine has the same `i1` case for `smulh` handled.) Found as written in `llvm/test/Transforms/InstCombine/smulh.ll` (`@one_smulh_i1`) compiled for RISC-V.

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i1 @f(i1 %x) {
  %r = call i1 @llvm.smulh.i1(i1 %x, i1 1)
  ret i1 %r
}
```
```
llc -mtriple=riscv64 -mattr=+m input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (mulhs x, 1 -> sra x, 0 -> x)
f:
	ret

; GlobalISel (sext(x) * -1, shifted right by 1: 0)
f:
	li	a1, -1
	slli	a0, a0, 63
	srai	a0, a0, 63
	mul	a0, a0, a1
	srai	a0, a0, 1
	ret
```
riscv-tv on the SelectionDAG output: `ERROR: Value mismatch`, for `%x = 1` the source returns `0` and the target `1`; the executed object code (Unicorn) returns `1`. The GlobalISel output verifies. The DAG combine trace: `Combining: t5: i1 = mulhs t3, Constant:i1<-1>` ... `into: t3: i1 = truncate t2`. Same with `-mattr=+rva23u64` and without `+m`.

**CC**
