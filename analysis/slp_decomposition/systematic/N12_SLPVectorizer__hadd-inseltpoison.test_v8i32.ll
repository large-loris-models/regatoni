define <8 x i32> @test_v8i32(<8 x i32> %a, <8 x i32> %b) {
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
  %r0 = add i32 %a0, %a1
  %r1 = add i32 %a2, %a3
  %r2 = add i32 %b0, %b1
  %r3 = add i32 %b2, %b3
  %r4 = add i32 %a4, %a5
  %r5 = add i32 %a6, %a7
  %r6 = add i32 %b4, %b5
  %r7 = add i32 %b6, %b7
  %r00 = insertelement <8 x i32> poison, i32 %r0, i32 0
  %r01 = insertelement <8 x i32>  %r00, i32 %r1, i32 1
  %r02 = insertelement <8 x i32>  %r01, i32 %r2, i32 2
  %r03 = insertelement <8 x i32>  %r02, i32 %r3, i32 3
  %r04 = insertelement <8 x i32>  %r03, i32 %r4, i32 4
  %r05 = insertelement <8 x i32>  %r04, i32 %r5, i32 5
  %r06 = insertelement <8 x i32>  %r05, i32 %r6, i32 6
  %r07 = insertelement <8 x i32>  %r06, i32 %r7, i32 7
  ret <8 x i32> %r07
}
