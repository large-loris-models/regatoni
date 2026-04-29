define <4 x float> @f(<4 x float> %a, <4 x float> %b) {
  %a0 = extractelement <4 x float> %a, i32 0
  %a1 = extractelement <4 x float> %a, i32 1
  %a2 = extractelement <4 x float> %a, i32 2
  %a3 = extractelement <4 x float> %a, i32 3
  %b0 = extractelement <4 x float> %b, i32 0
  %b1 = extractelement <4 x float> %b, i32 1
  %b2 = extractelement <4 x float> %b, i32 2
  %b3 = extractelement <4 x float> %b, i32 3
  %c0 = fadd float %a0, %b0
  %c1 = fadd float %a1, %b1
  %c2 = fadd float %a2, %b2
  %c3 = fadd float %a3, %b3
  %r0 = insertelement <4 x float> poison, float %c0, i32 0
  %r1 = insertelement <4 x float>   %r0, float %c1, i32 1
  %r2 = insertelement <4 x float>   %r1, float %c2, i32 2
  %r3 = insertelement <4 x float>   %r2, float %c3, i32 3
  ret <4 x float> %r3
}
