target triple = "x86_64-unknown-linux-gnu"
; 8-wide alt-opcode bitwise (and/or) bundle to trigger canBuildSplitNode
; → isBitwiseLogicOp.
define <8 x i32> @altbitwise8(<8 x i32> %a, <8 x i32> %b) {
  %a0 = extractelement <8 x i32> %a, i32 0
  %a1 = extractelement <8 x i32> %a, i32 1
  %a2 = extractelement <8 x i32> %a, i32 2
  %a3 = extractelement <8 x i32> %a, i32 3
  %a4 = extractelement <8 x i32> %a, i32 4
  %a5 = extractelement <8 x i32> %a, i32 5
  %a6 = extractelement <8 x i32> %a, i32 6
  %a7 = extractelement <8 x i32> %a, i32 7
  %b0 = extractelement <8 x i32> %b, i32 0
  %b1 = extractelement <8 x i32> %b, i32 1
  %b2 = extractelement <8 x i32> %b, i32 2
  %b3 = extractelement <8 x i32> %b, i32 3
  %b4 = extractelement <8 x i32> %b, i32 4
  %b5 = extractelement <8 x i32> %b, i32 5
  %b6 = extractelement <8 x i32> %b, i32 6
  %b7 = extractelement <8 x i32> %b, i32 7
  %r0 = and i32 %a0, %b0
  %r1 = or  i32 %a1, %b1
  %r2 = and i32 %a2, %b2
  %r3 = or  i32 %a3, %b3
  %r4 = and i32 %a4, %b4
  %r5 = or  i32 %a5, %b5
  %r6 = and i32 %a6, %b6
  %r7 = or  i32 %a7, %b7
  %i0 = insertelement <8 x i32> poison, i32 %r0, i32 0
  %i1 = insertelement <8 x i32>   %i0, i32 %r1, i32 1
  %i2 = insertelement <8 x i32>   %i1, i32 %r2, i32 2
  %i3 = insertelement <8 x i32>   %i2, i32 %r3, i32 3
  %i4 = insertelement <8 x i32>   %i3, i32 %r4, i32 4
  %i5 = insertelement <8 x i32>   %i4, i32 %r5, i32 5
  %i6 = insertelement <8 x i32>   %i5, i32 %r6, i32 6
  %i7 = insertelement <8 x i32>   %i6, i32 %r7, i32 7
  ret <8 x i32> %i7
}
