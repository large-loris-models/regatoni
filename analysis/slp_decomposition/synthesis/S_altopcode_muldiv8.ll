target triple = "x86_64-unknown-linux-gnu"
; 8-wide alt-opcode mul/div bundle to trigger canBuildSplitNode
; → isMulDivLikeOp.
define <8 x float> @altmuldiv8(<8 x float> %a, <8 x float> %b) {
  %a0 = extractelement <8 x float> %a, i32 0
  %a1 = extractelement <8 x float> %a, i32 1
  %a2 = extractelement <8 x float> %a, i32 2
  %a3 = extractelement <8 x float> %a, i32 3
  %a4 = extractelement <8 x float> %a, i32 4
  %a5 = extractelement <8 x float> %a, i32 5
  %a6 = extractelement <8 x float> %a, i32 6
  %a7 = extractelement <8 x float> %a, i32 7
  %b0 = extractelement <8 x float> %b, i32 0
  %b1 = extractelement <8 x float> %b, i32 1
  %b2 = extractelement <8 x float> %b, i32 2
  %b3 = extractelement <8 x float> %b, i32 3
  %b4 = extractelement <8 x float> %b, i32 4
  %b5 = extractelement <8 x float> %b, i32 5
  %b6 = extractelement <8 x float> %b, i32 6
  %b7 = extractelement <8 x float> %b, i32 7
  %r0 = fmul float %a0, %b0
  %r1 = fdiv float %a1, %b1
  %r2 = fmul float %a2, %b2
  %r3 = fdiv float %a3, %b3
  %r4 = fmul float %a4, %b4
  %r5 = fdiv float %a5, %b5
  %r6 = fmul float %a6, %b6
  %r7 = fdiv float %a7, %b7
  %i0 = insertelement <8 x float> poison, float %r0, i32 0
  %i1 = insertelement <8 x float>   %i0, float %r1, i32 1
  %i2 = insertelement <8 x float>   %i1, float %r2, i32 2
  %i3 = insertelement <8 x float>   %i2, float %r3, i32 3
  %i4 = insertelement <8 x float>   %i3, float %r4, i32 4
  %i5 = insertelement <8 x float>   %i4, float %r5, i32 5
  %i6 = insertelement <8 x float>   %i5, float %r6, i32 6
  %i7 = insertelement <8 x float>   %i6, float %r7, i32 7
  ret <8 x float> %i7
}
