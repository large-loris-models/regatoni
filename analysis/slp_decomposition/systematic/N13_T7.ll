@T7_buf = local_unnamed_addr global [128 x i8] zeroinitializer, align 16
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
  ; ---- T7 RuntimeStrideGEP ----
  %t7_stride = load volatile i64, ptr @T7_buf, align 8
  %t7_s0 = mul nsw i64 %t7_stride, 0
  %t7_s1 = mul nsw i64 %t7_stride, 1
  %t7_s2 = mul nsw i64 %t7_stride, 2
  %t7_s3 = mul nsw i64 %t7_stride, 3
  %t7_p0 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s0
  %t7_p1 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s1
  %t7_p2 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s2
  %t7_p3 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s3
  %t7_v0 = load i8, ptr %t7_p0, align 1
  %t7_v1 = load i8, ptr %t7_p1, align 1
  %t7_v2 = load i8, ptr %t7_p2, align 1
  %t7_v3 = load i8, ptr %t7_p3, align 1
  %t7_o0 = getelementptr inbounds i8, ptr @T7_buf, i64 100
  %t7_o1 = getelementptr inbounds i8, ptr @T7_buf, i64 101
  %t7_o2 = getelementptr inbounds i8, ptr @T7_buf, i64 102
  %t7_o3 = getelementptr inbounds i8, ptr @T7_buf, i64 103
  store i8 %t7_v0, ptr %t7_o0, align 1
  store i8 %t7_v1, ptr %t7_o1, align 1
  store i8 %t7_v2, ptr %t7_o2, align 1
  store i8 %t7_v3, ptr %t7_o3, align 1
  ret <8 x float> %r7
}
