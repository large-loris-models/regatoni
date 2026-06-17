Title: [RISC-V][GlobalISel] Miscompilation where the sum of two bitwise-NOT values is folded to the sum of the originals


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - <add link>
2. GlobalISel - <add link>

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Description**
Since `xor i8 %a, 0` is the identity, the function computes `(~%x) + (~%y)`, but GlobalISel folds it to `%x + %y` — dropping the two complements — so the result is off by `2`

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i8 @f(i8 %x, i8 %y) {
  %nx = xor i8 %x, -1
  %ny = xor i8 %y, -1
  %a  = add i8 %nx, %ny
  %r  = xor i8 %a, 0
  ret i8 %r
}
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (~x + ~y)
f:
	not	a0, a0
	not	a1, a1
	add	a0, a0, a1
	ret

; GlobalISel (x + y; xnor with -1 is the identity)
f:
	li	a2, -1
	add	a0, a1, a0
	xnor	a0, a0, a2
	ret
```
With `x = 0, y = 0`: source `~0 + ~0 = -2` (`0xfe`), GlobalISel returns `0`

**CC**: @regehr
