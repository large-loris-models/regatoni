Title: [RISC-V][GlobalISel] Miscompilation where an i31 no-wrap negation inside a min chain is lowered incorrectly


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/T7Y5ajG5Y
2. GlobalISel - https://godbolt.org/z/9ns9WG1P4

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Description**
Since the `nuw` flag forces `sext(sub nuw i31 0, trunc %sel)` to 0, the function just returns `0`, but GlobalISel keeps the term and mis-lowers the i31 negate to a 32-bit `negw`, returning `-umin(a,y)`

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i32 @f(i32 %a, i32 %y) {
  %cmp1 = icmp ult i32 %a, %y
  %sel = select i1 %cmp1, i32 %a, i32 %y
  %1 = trunc i32 %sel to i31
  %2 = sub nuw i31 0, %1
  %3 = sext i31 %2 to i32
  ret i32 %3
}
```
```bash
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
; SelectionDAG (returns 0)

```asm
f:                                      # @f
        li      a0, 0
        ret
```

; GlobalISel (-umin(a,y))

```asm
f:                                      # @f
        sext.w  a0, a0
        sext.w  a1, a1
        minu    a0, a0, a1
        negw    a0, a0
        ret
```

**CC**: @regehr