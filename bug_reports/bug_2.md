Title: [RISC-V][GlobalISel] Miscompilation where an i31 no-wrap negation inside a min/compare chain is lowered incorrectly, giving the wrong boolean result


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/T7Y5ajG5Y
2. GlobalISel - https://godbolt.org/z/9ns9WG1P4

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Description**
Since the no-wrap flags force `and(%sel, sext(sub nuw nsw i31 0, trunc %sel))` to 0, the function is just `x != 0`, but GlobalISel keeps the term and mis-lowers the i31 negate to a 32-bit `negw`, returning `-umin(x,y) <u x`

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i1 @f(i32 %a, i32 %y) {
  %x = add i32 %a, 3
  %cmp1 = icmp ult i32 %x, %y
  %sel = select i1 %cmp1, i32 %x, i32 %y
  %1 = trunc i32 %sel to i31
  %2 = sub nuw nsw i31 0, %1
  %3 = sext i31 %2 to i32
  %4 = and i32 %sel, %3
  %cmp2 = icmp ugt i32 %x, %4
  ret i1 %cmp2
}
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (x != 0)
f:
	addiw	a0, a0, 3
	snez	a0, a0
	ret

; GlobalISel (-umin(x,y) <u x)
f:
	addiw	a0, a0, 3
	sext.w	a1, a1
	minu	a1, a0, a1
	negw	a1, a1
	sltu	a0, a1, a0
	ret
```
With `%a = 0x7FFFFFFD`, `%y = 0x80000000` (so `%x = 0x80000000`, poison-free): source returns `1`, GlobalISel returns `0`

**CC**: @regehr
