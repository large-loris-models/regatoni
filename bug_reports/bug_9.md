Title: [RISCV][GlobalISel] Miscompilation where reversing the bits of a 24-bit integer zeroes its middle byte


**Fuzzer Generated Test**
**Reproducer**
1. SelectionDAG - https://godbolt.org/z/bdhbr9rv3
2. GlobalISel - https://godbolt.org/z/xsqEYebbc

**Test Commit**
[e633c7da8ff144a11b3d875956fbaea06b969070](https://github.com/llvm/llvm-project/commit/e633c7da8ff144a11b3d875956fbaea06b969070)

**Description**
`@llvm.bitreverse.i24` reverses the order of all 24 bits of `x`, but GlobalISel (`LegalizerHelper::lowerBitreverse`) lowers it to a 24-bit `G_BSWAP` followed by in-byte bit swaps, and `LegalizerHelper::lowerBswap` only swaps the outer two bytes of that odd-sized byte swap and never puts the middle byte back, so bits 8-15 of the result are always zero (`i40` and `i56` lose their middle byte the same way; #199386 was a different bug in the same `lowerBswap`)

**Steps to reproduce**
- Minimized test case, `input.ll`
```
define i24 @f(i24 %x) {
  %rev = call i24 @llvm.bitreverse.i24(i24 %x)
  ret i24 %rev
}
```
```
llc -mtriple=riscv64 input.ll   # SelectionDAG
llc -mtriple=riscv64 -global-isel input.ll   # GlobalISel
```

**Output**
```
; SelectionDAG (full 64-bit bit reverse, then shift right by 40: the 24-bit bit reverse of x)
f:
	lui	a1, 16
	srli	a2, a0, 40
	addi	a1, a1, -256
	srli	a3, a0, 56
	and	a2, a2, a1
	srli	a4, a0, 24
	srli	a5, a0, 8
	lui	a6, 4080
	srliw	a5, a5, 24
	and	a4, a4, a6
	slli	a5, a5, 24
	or	a2, a2, a3
	or	a4, a5, a4
	or	a2, a4, a2
	and	a3, a0, a6
	srliw	a4, a0, 24
	slli	a3, a3, 24
	slli	a4, a4, 32
	or	a3, a3, a4
	and	a1, a0, a1
	slli	a1, a1, 40
	slli	a0, a0, 56
	or	a0, a0, a1
	lui	a1, 61681
	or	a0, a0, a3
	addi	a1, a1, -241
	or	a0, a0, a2
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
	srli	a3, a0, 2
	add	a1, a1, a2
	and	a3, a3, a1
	and	a0, a0, a1
	lui	a1, 1365
	slli	a0, a0, 2
	addi	a1, a1, 1365
	or	a0, a3, a0
	slli	a1, a1, 40
	srli	a2, a0, 1
	and	a0, a0, a1
	and	a1, a2, a1
	slli	a0, a0, 1
	or	a0, a1, a0
	srli	a0, a0, 40
	ret

; GlobalISel ((x >> 16) | (x << 16) swaps bytes 0 and 2 and leaves byte 1 zero, then the bits of each byte are reversed)
f:
	lui	a1, 4096
	lui	a2, 1048335
	addi	a1, a1, -1
	slli	a3, a0, 16
	and	a0, a0, a1
	addi	a2, a2, 240
	srli	a0, a0, 16
	or	a0, a0, a3
	and	a3, a2, a1
	and	a3, a0, a3
	slli	a0, a0, 4
	and	a0, a0, a2
	lui	a2, 1047757
	srli	a3, a3, 4
	addi	a2, a2, -820
	or	a0, a3, a0
	and	a3, a2, a1
	and	a3, a0, a3
	slli	a0, a0, 2
	and	a0, a0, a2
	lui	a2, 1047211
	srli	a3, a3, 2
	addi	a2, a2, -1366
	or	a0, a3, a0
	and	a1, a2, a1
	and	a1, a0, a1
	slli	a0, a0, 1
	srli	a1, a1, 1
	and	a0, a0, a2
	or	a0, a1, a0
	ret
```
With x = 0x000102: source returns 0x408000, GlobalISel returns 0x400000

**CC**: @regehr