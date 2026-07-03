Title: [RISC-V][GlobalISel] Miscompilation where a bitwise-and of a value with its own negation is dropped, returning only the negation


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/jTTfefPr3
2. GlobalISel - https://godbolt.org/z/14Y9Y73zn

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Oracle**
arm-tv, riscv-tv — also miscompiles on AArch64 GlobalISel; the bug is in target-independent GlobalISel (`mul_by_neg_one` → `G_SUB` nuw known-bits → `redundant_and`)

**Description**
Since `mul nuw i32 x, -1` equals `-x`, the function is the lowest-set-bit idiom `x & -x`, but GlobalISel drops the `and` and returns just `-x`

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i32 @f(i32 %x) {
  %neg = mul nuw i32 %x, -1
  %r   = and i32 %x, %neg
  ret i32 %r
}
```
```
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG 
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (x & -x)
f:
	negw	a1, a0
	and	a0, a0, a1
	ret

; GlobalISel (drops the `and`, returns -x)
f:
	negw	a0, a0
	ret
```
With `x = 1` (`mul nuw 1, -1` = `0xFFFFFFFF`, poison-free): source `1 & -1 = 1`, GlobalISel returns `0xFFFFFFFF` (`-1`)

**CC**: @regehr 
