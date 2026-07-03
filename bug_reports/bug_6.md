Title: [RISC-V][GlobalISel] Miscompilation where a reassociated addition keeps a no-unsigned-wrap flag it cannot, dropping a masking AND


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - <add link>
2. GlobalISel - <add link>

**Test Commit**
[f0ca72c6f4e177f735e6486f839acb296c4d02f0](https://github.com/llvm/llvm-project/commit/f0ca72c6f4e177f735e6486f839acb296c4d02f0)

**Description**
Since `add nuw i16 %A, (1 - 2·%A)` equals `1 - %A`, the function computes `(1 - %A) & 1`, but GlobalISel reassociates the addition to `sub 1, %A` while keeping the `nuw` flag — which it cannot, since the original addition can be in-range while `1 - %A` wraps — and that false `nuw` lets a later combine treat `1 - %A` as already `0` or `1` and drop the `& 1`, returning the full `1 - %A`

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i16 @f(i16 %A) {
  %1 = add i16 %A, %A
  %2 = sub i16 1, %1
  %3 = add nuw i16 %A, %2
  %4 = and i16 %3, 1
  ret i16 %4
}
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG ((1 - A) & 1)
f:
	li	a1, 1
	sub	a0, a1, a0
	andi	a0, a0, 1
	ret

; GlobalISel (drops the `and`, returns 1 - A)
f:
	li	a1, 1
	sub	a0, a1, a0
	ret
```
With `A = 4` (the addition is in-range, so poison-free): source `(1 - 4) & 1 = 1`, GlobalISel returns `-3` (`0xfffd`)

**CC**: @regehr
