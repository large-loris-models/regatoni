Title: [RISC-V][GlobalISel] Miscompilation where a sign-extension that round-trips through a wider type is lowered as a zero-extending mask


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/TrbvTxPWj
2. GlobalISel - https://godbolt.org/z/hq5xvodEb

**Test Commit**
[efb038f38f23ee201ac872ae98668c2ef922f0fa](https://github.com/llvm/llvm-project/commit/efb038f38f23ee201ac872ae98668c2ef922f0fa)

**Description**
The function sign-extends an `i9` to `i16` (the `i128` round-trip and `mul ..., 1` are identities), but GlobalISel lowers it to `andi a0, a0, 511`, a zero-extending mask that drops the sign extension, so a negative input returns a positive result

**Steps to reproduce**
- Minimized test case, `input.ll`
```llvm
define i16 @f(i9 %x) {
  %sx = sext i9 %x to i16
  %1  = sext i16 %sx to i128
  %2  = mul i128 %1, 1
  %3  = trunc i128 %2 to i16
  ret i16 %3
}
```
```bash
llc -mtriple=riscv64 -mattr=+m,+b input.ll   # SelectionDAG
llc -mtriple=riscv64 -mattr=+m,+b -global-isel input.ll   # GlobalISel
```

**Output**
; SelectionDAG (sign-extend: slli/srai)

```asm
f:
	slli	a0, a0, 55
	srai	a0, a0, 55
	ret
```

; GlobalISel (zero-extending mask, drops the sign)

```asm
f:
	andi	a0, a0, 511
	ret
```

With `%x = -256` (the `i9` with its sign bit set): source `sext` = `-256` (`0xff00`), GlobalISel returns `256` (`0x0100`)

**CC**: @regehr
