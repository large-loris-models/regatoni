@T1_buf = local_unnamed_addr global [8 x i64] zeroinitializer, align 16
define <8 x float> @f(<4 x i32> %a, <8 x i16> %b) {
  %a0 = extractelement <4 x i32> %a, i32 0
  %a1 = extractelement <4 x i32> %a, i32 1
  %a2 = extractelement <4 x i32> %a, i32 2
  %a3 = extractelement <4 x i32> %a, i32 3
  %b0 = extractelement <8 x i16> %b, i32 0
  %b1 = extractelement <8 x i16> %b, i32 1
  %b2 = extractelement <8 x i16> %b, i32 2
  %b3 = extractelement <8 x i16> %b, i32 3
  %c0 = sitofp i32 %a0 to float
  %c1 = sitofp i32 %a1 to float
  %c2 = sitofp i32 %a2 to float
  %c3 = sitofp i32 %a3 to float
  %c4 = sitofp i16 %b0 to float
  %c5 = sitofp i16 %b1 to float
  %c6 = sitofp i16 %b2 to float
  %c7 = sitofp i16 %b3 to float
  %r0 = insertelement <8 x float> poison, float %c0, i32 0
  %r1 = insertelement <8 x float>   %r0, float %c1, i32 1
  %r2 = insertelement <8 x float>   %r1, float %c2, i32 2
  %r3 = insertelement <8 x float>   %r2, float %c3, i32 3
  %r4 = insertelement <8 x float>   %r3, float %c4, i32 4
  %r5 = insertelement <8 x float>   %r4, float %c5, i32 5
  %r6 = insertelement <8 x float>   %r5, float %c6, i32 6
  %r7 = insertelement <8 x float>   %r6, float %c7, i32 7
  ; ---- T1 StoreChain ----
  %t1_g0 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 0
  %t1_g1 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 1
  %t1_v0 = load i64, ptr %t1_g0, align 16
  %t1_v1 = load i64, ptr %t1_g1, align 8
  %t1_a0 = add i64 %t1_v0, 1
  %t1_a1 = add i64 %t1_v1, 1
  %t1_s0 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 4
  %t1_s1 = getelementptr inbounds [8 x i64], ptr @T1_buf, i64 0, i64 5
  store i64 %t1_a0, ptr %t1_s0, align 16
  store i64 %t1_a1, ptr %t1_s1, align 8
  ret <8 x float> %r7
}
