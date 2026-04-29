target triple = "x86_64-unknown-linux-gnu"
define <4 x float> @addsub(<4 x float> %a, <4 x float> %b) {
  %a0 = extractelement <4 x float> %a, i32 0
  %a1 = extractelement <4 x float> %a, i32 1
  %a2 = extractelement <4 x float> %a, i32 2
  %a3 = extractelement <4 x float> %a, i32 3
  %b0 = extractelement <4 x float> %b, i32 0
  %b1 = extractelement <4 x float> %b, i32 1
  %b2 = extractelement <4 x float> %b, i32 2
  %b3 = extractelement <4 x float> %b, i32 3
  ; alternating add/sub
  %r0 = fadd float %a0, %b0
  %r1 = fsub float %a1, %b1
  %r2 = fadd float %a2, %b2
  %r3 = fsub float %a3, %b3
  %i0 = insertelement <4 x float> poison, float %r0, i32 0
  %i1 = insertelement <4 x float>   %i0, float %r1, i32 1
  %i2 = insertelement <4 x float>   %i1, float %r2, i32 2
  %i3 = insertelement <4 x float>   %i2, float %r3, i32 3
  ret <4 x float> %i3
}
