; S_R3_interchangeable_binops: 4-lane integer binop bundle where each
; lane uses an "identity" constant — `add %x, 0`, `or %y, 0`,
; `xor %z, 0`, `mul %w, 1`. SLP's BinOpSameOpcodeHelper recognises all
; four as interchangeable to the same opcode (Add via lhs+0, Or via
; lhs|0, Xor via lhs^0, Mul via lhs*1) so the bundle clusters as a
; same-opcode 4-lane buildvector. This forces the
; BinOpSameOpcodeHelper::add/getMainOpcode/getAltOpcode pipeline.
target triple = "x86_64-unknown-linux-gnu"

define <4 x i32> @interchangeable_binops(i32 %a, i32 %b, i32 %c, i32 %d) {
entry:
  %x0 = add i32 %a, 0
  %x1 = or  i32 %b, 0
  %x2 = xor i32 %c, 0
  %x3 = mul i32 %d, 1
  %v0 = insertelement <4 x i32> poison, i32 %x0, i64 0
  %v1 = insertelement <4 x i32> %v0,    i32 %x1, i64 1
  %v2 = insertelement <4 x i32> %v1,    i32 %x2, i64 2
  %v3 = insertelement <4 x i32> %v2,    i32 %x3, i64 3
  ret <4 x i32> %v3
}
