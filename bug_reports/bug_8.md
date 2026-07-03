Title: [RISC-V][GlobalISel] Miscompilation where a saturating left-shift is incorrectly distributed over a bitwise AND


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/fx6czM4n9
2. GlobalISel - https://godbolt.org/z/7fj56xEoe

**Test Commit**
[f0ca72c6f4e177f735e6486f839acb296c4d02f0](https://github.com/llvm/llvm-project/commit/f0ca72c6f4e177f735e6486f839acb296c4d02f0)

**Oracle**
riscv-tv — AArch64 GlobalISel cannot legalize `ushl.sat` (it errors before codegen), so the miscompile is not observable there; only RISC-V legalizes the saturating shift and exposes the combine

**Description**
The function shifts `(ushl.sat(x, 1) & y)` left by one with `ushl.sat`, but GlobalISel's shift-of-shifted-logic combine treats the saturating shift like an ordinary shift and rewrites it to `ushl.sat(x, 2) & ushl.sat(y, 1)` — which is wrong because `ushl.sat` clamps on overflow instead of wrapping, so it does not distribute over the `and` and saturating `y` on its own changes the result

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i8 @f(i8 %x, i8 %y) {
  %a  = call i8 @llvm.ushl.sat.i8(i8 %x, i8 1)
  %t2 = and i8 %a, %y
  %r  = call i8 @llvm.ushl.sat.i8(i8 %t2, i8 1)
  ret i8 %r
}
declare i8 @llvm.ushl.sat.i8(i8, i8)
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (saturating-shifts x, ANDs with y, then saturating-shifts the result)
f:
	slli	a2, a0, 56
	srli	a2, a2, 63
	seqz	a2, a2
	slli	a0, a0, 57
	addi	a2, a2, -1
	or	a0, a2, a0
	srli	a0, a0, 56
	and	a0, a0, a1
	slli	a1, a0, 56
	srli	a1, a1, 63
	seqz	a1, a1
	slli	a0, a0, 57
	addi	a1, a1, -1
	or	a0, a1, a0
	srli	a0, a0, 56
	ret

; GlobalISel (distributes: saturating-shifts x by 2 and y by 1 independently, then ANDs)
f:
	slli	a3, a0, 2
	zext.b	a2, a3
	srli	a4, a2, 2
	zext.b	a5, a0
	li	a0, -1
	li	a2, -1
	bne	a5, a4, .LBB0_2
	mv	a2, a3
.LBB0_2:
	slli	a3, a1, 1
	zext.b	a4, a3
	srli	a4, a4, 1
	zext.b	a1, a1
	bne	a1, a4, .LBB0_4
	mv	a0, a3
.LBB0_4:
	and	a0, a2, a0
	ret
```
With `x = 14`, `y = 237`: source computes `ushl.sat(14,1)=28`, `28 & 237 = 12`, `ushl.sat(12,1)=24`, while GlobalISel computes `ushl.sat(14,2)=56`, `ushl.sat(237,1)=255`, `56 & 255 = 56`. The same defect affects `sshl.sat` and the `or`/`xor` variants wherever the saturating shift does not distribute over the logic op

**CC**: @regehr
