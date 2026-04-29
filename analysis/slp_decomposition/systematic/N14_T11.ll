declare <4 x float> @llvm.fmuladd.v4f32(<4 x float>, <4 x float>, <4 x float>)
@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define void @f(<4 x float> %a, <4 x float> %b, <4 x float> %c, <4 x float> %d, <4 x float> %e, <4 x float> %g, ptr %out) {
  %s1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 2, i32 3, i32 4, i32 5>
  %s2 = shufflevector <4 x float> %b, <4 x float> %c, <4 x i32> <i32 2, i32 3, i32 4, i32 5>
  %m1 = call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %s1, <4 x float> %d, <4 x float> %e)
  %m2 = call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %s2, <4 x float> %d, <4 x float> %g)
  %g1 = getelementptr inbounds i8, ptr %out, i64 16
  store <4 x float> %m1, ptr %out, align 4
  store <4 x float> %m2, ptr %g1, align 4
  ; ---- T11 NonUniformStoreChain ----
  %t11_g0 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 0
  %t11_g1 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 1
  %t11_v0 = load i32, ptr %t11_g0, align 16
  %t11_v1 = load i32, ptr %t11_g1, align 4
  %t11_d0 = udiv i32 %t11_v0, 3
  %t11_d1 = udiv i32 %t11_v1, 8
  %t11_o0 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 0
  %t11_o1 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 1
  %t11_o2 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 2
  %t11_o3 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 3
  store i32 %t11_d0, ptr %t11_o0, align 16
  store i32 %t11_d1, ptr %t11_o1, align 4
  store i32 %t11_d0, ptr %t11_o2, align 8
  store i32 %t11_d1, ptr %t11_o3, align 4
  ret void
}
