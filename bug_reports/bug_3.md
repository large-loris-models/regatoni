Title: [RISC-V][SelectionDAG] Miscompilation where a frozen poison value is duplicated, collapsing a clear-lowest-bit computation to `x - 1`


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/Pc73dsvvE
2. GlobalISel - https://godbolt.org/z/6vMrajhGb

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Description**
Since `llvm.ushl.sat.i32(x, 32)` shifts by the full bit width it is poison, so after `freeze` pins it to a single value the result `V & (V-1)` is always even, but SelectionDAG fails to pin the frozen value and returns `arg0 - 1` (odd for even `arg0`)

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i32 @f(i32 %arg0) {
  %call   = call i32 @llvm.ushl.sat.i32(i32 %arg0, i32 32)
  %freeze = freeze i32 %call
  %1      = sub i32 %freeze, 1
  %2      = and i32 %freeze, %1
  ret i32 %2
}
declare i32 @llvm.ushl.sat.i32(i32, i32)
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (returns arg0 - 1)
f:
	addiw	a0, a0, -1
	and	a0, a0, a0
	ret

; GlobalISel (materializes the saturating shift)
f:
	slli	a1, a0, 32
	zext.w	a2, a1
	srli	a2, a2, 32
	sext.w	a0, a0
	beq	a0, a2, .LBB0_2
	li	a1, -1
	addiw	a0, a1, -1
	and	a0, a1, a0
	ret
```
The source is always even (`V & (V-1)`); with `arg0 = 0` SelectionDAG returns `-1` (odd), which the source can never produce

**CC**: @regehr
