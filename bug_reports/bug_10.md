Title: [RISCV][SelectionDAG] Miscompilation where counting the trailing zeros of a range-annotated load reads far past the end of a 32-byte lookup table


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/dv3jG5rKK
2. GlobalISel - https://godbolt.org/z/ssKf1d7er

**Test Commit**
[e633c7da8ff144a11b3d875956fbaea06b969070](https://github.com/llvm/llvm-project/commit/e633c7da8ff144a11b3d875956fbaea06b969070)

**Description**
`f` loads an i32 whose `!range` says it is below 32 (any other value makes the load, and so the result, poison) and returns its trailing-zero count, but SelectionDAG expands the `cttz` into a lookup in a 32-entry table (`TargetLowering::CTTZTableLookup`) without freezing the operand, and the DAG combine after type legalization uses the range to delete the 32-bit truncation of the table index (`srliw` becomes `srli`), so for a value outside the range the `lbu` reads megabytes past the table and can fault where the source only returns poison

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i32 @f(ptr %p) {
  %v = load i32, ptr %p, !range !0
  %c = call i32 @llvm.cttz.i32(i32 %v, i1 true)
  ret i32 %c
}
!0 = !{i32 0, i32 32}
```
```
llc -mtriple=riscv64 input.ll   # SelectionDAG
llc -mtriple=riscv64 -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (loads table[((v & -v) * 0x077CB531) >> 27] with the product not truncated to 32 bits, so the index can exceed 31)
f:
	lui	a1, %hi(.LCPI0_0)
	addi	a1, a1, %lo(.LCPI0_0)
	lw	a0, 0(a0)
	neg	a2, a0
	and	a0, a0, a2
	slli	a2, a0, 4
	slli	a3, a0, 6
	slli	a4, a0, 8
	slli	a5, a0, 10
	slli	a6, a0, 12
	add	a3, a3, a4
	slli	a4, a0, 16
	sub	a5, a5, a6
	slli	a6, a0, 18
	sub	a4, a4, a6
	sub	a2, a0, a2
	add	a2, a2, a3
	slli	a3, a0, 14
	sub	a5, a5, a3
	slli	a3, a0, 23
	sub	a4, a4, a3
	slli	a0, a0, 27
	add	a2, a2, a5
	add	a0, a4, a0
	add	a0, a2, a0
	srli	a0, a0, 27
	add	a0, a1, a0
	lbu	a0, 0(a0)
	ret

; GlobalISel (popcount of ~v & (v - 1), no table: the trailing-zero count of v)
f:
	addi	sp, sp, -16
	sd	ra, 8(sp)
	lui	a1, 349525
	lw	a0, 0(a0)
	not	a2, a0
	addi	a0, a0, -1
	and	a0, a2, a0
	addi	a1, a1, 1365
	srliw	a2, a0, 1
	and	a1, a2, a1
	lui	a2, 209715
	sub	a0, a0, a1
	addi	a1, a2, 819
	srliw	a2, a0, 2
	and	a0, a0, a1
	and	a1, a2, a1
	add	a0, a1, a0
	sraiw	a1, a0, 4
	lui	a2, 61681
	addw	a0, a1, a0
	addi	a1, a2, -241
	lui	a2, 4112
	and	a0, a0, a1
	addi	a1, a2, 257
	call	__muldi3
	srliw	a0, a0, 24
	ld	ra, 8(sp)
	addi	sp, sp, 16
	ret
```
With `%p` pointing at the i32 value 0x40800000: source returns poison (the value is outside `!range`), SelectionDAG loads from about 7.8 MB past the table and faults

**CC**: @regehr