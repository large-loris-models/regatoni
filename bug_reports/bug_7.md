Title: [RISC-V][GlobalISel] Miscompilation where bit-reversing an integer whose width is not a multiple of 8 reverses inside a byte-padded frame, leaving the result shifted


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/79Kqxbxvc
2. GlobalISel - https://godbolt.org/z/6rbxYhGfr

**Test Commit**
[f0ca72c6f4e177f735e6486f839acb296c4d02f0](https://github.com/llvm/llvm-project/commit/f0ca72c6f4e177f735e6486f839acb296c4d02f0)

**Oracle**
riscv-tv — AArch64 GlobalISel is correct; it widens `bitreverse` to i32 and uses native `rbit`, so only RISC-V routes a non-byte width to the byte-granular `lowerBitreverse`

**Description**
The function bit-reverses an `i17`, but GlobalISel lowers `llvm.bitreverse` by byte-swapping the value and applying byte-granular swap masks — which is only correct when the width is a multiple of 8 — so for a non-byte-multiple width the reversed bits land shifted from their correct positions

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i17 @f(i17 %x) {
  %r = call i17 @llvm.bitreverse.i17(i17 %x)
  ret i17 %r
}
declare i17 @llvm.bitreverse.i17(i17)
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (bit-reverses the 17-bit value: bit i -> bit 16-i)
f:
	lui	a1, 61681
	addi	a1, a1, -241
	rev8	a0, a0
	slli	a2, a1, 32
	srli	a3, a0, 4
	add	a1, a1, a2
	and	a3, a3, a1
	and	a0, a0, a1
	lui	a1, 209715
	slli	a0, a0, 4
	addi	a1, a1, 819
	or	a0, a3, a0
	slli	a2, a1, 32
	add	a1, a1, a2
	srli	a2, a0, 2
	and	a0, a0, a1
	and	a1, a2, a1
	sh2add	a0, a0, a1
	lui	a1, 87381
	slli	a1, a1, 34
	lui	a2, 21845
	srli	a3, a0, 1
	slli	a2, a2, 36
	and	a0, a0, a1
	and	a2, a3, a2
	sh1add	a0, a0, a2
	srli	a0, a0, 47
	ret

; GlobalISel (reverses within a byte-padded frame, leaving the result shifted)
f:
	lui	a1, 15
	rev8	a0, a0
	srli	a0, a0, 47
	addi	a1, a1, 240
	slli	a2, a0, 4
	and	a0, a0, a1
	and	a1, a2, a1
	lui	a2, 13
	srli	a0, a0, 4
	addi	a2, a2, -820
	or	a0, a0, a1
	and	a1, a0, a2
	slli	a0, a0, 2
	srli	a1, a1, 2
	and	a0, a0, a2
	lui	a2, 11
	or	a0, a1, a0
	addi	a1, a2, -1366
	and	a2, a0, a1
	slli	a0, a0, 1
	srli	a2, a2, 1
	and	a0, a0, a1
	or	a0, a2, a0
	ret
```
With `x = 1`: source `bitreverse` sets bit 16, returning `0x10000` (`65536`), while GlobalISel returns `0x04000` (`16384`, bit 14). The same defect affects every width `iN` with `9 <= N <= 63` and `N % 8 != 0`

**CC**: @regehr
