Title: [RISC-V][GlobalISel] Miscompilation where a negation is dropped, turning `x & (x + 1)` into `x & ~x` (always zero)


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/YMY116ovr
2. GlobalISel - https://godbolt.org/z/ojcooKEMY

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Oracle**
arm-tv, riscv-tv — target-independent GlobalISel combine (`matchBinopWithNegInner`), so it reproduces on AArch64 as well

**Description**
Since `sub 0, (xor x, -1)` equals `x + 1`, the function is `x & (x + 1)`, but GlobalISel drops the negation and folds it to `andn x, x` (`x & ~x`, always 0)

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i32 @f(i32 %g) {
  %not = xor i32 %g, -1
  %neg = sub i32 0, %not
  %and = and i32 %g, %neg
  ret i32 %and
}
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (x & (x + 1))
f:
	addiw	a1, a0, 1
	and	a0, a0, a1
	ret

; GlobalISel (x & ~x, always 0)
f:
	andn	a0, a0, a0
	ret
```
With `g = 2`: source `2 & 3 = 2`, GlobalISel returns `0`

**CC**: @regehr
