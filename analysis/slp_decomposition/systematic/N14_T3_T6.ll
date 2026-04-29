declare <4 x float> @llvm.fmuladd.v4f32(<4 x float>, <4 x float>, <4 x float>)
@T3_buf = local_unnamed_addr global [4 x double] zeroinitializer, align 16
@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define void @f(<4 x float> %a, <4 x float> %b, <4 x float> %c, <4 x float> %d, <4 x float> %e, <4 x float> %g, ptr %out) {
  %s1 = shufflevector <4 x float> %a, <4 x float> %b, <4 x i32> <i32 2, i32 3, i32 4, i32 5>
  %s2 = shufflevector <4 x float> %b, <4 x float> %c, <4 x i32> <i32 2, i32 3, i32 4, i32 5>
  %m1 = call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %s1, <4 x float> %d, <4 x float> %e)
  %m2 = call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %s2, <4 x float> %d, <4 x float> %g)
  %g1 = getelementptr inbounds i8, ptr %out, i64 16
  store <4 x float> %m1, ptr %out, align 4
  store <4 x float> %m2, ptr %g1, align 4
  ; ---- T3 HorReductionChain ----
  %t3_g0 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 0
  %t3_g1 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 1
  %t3_g2 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 2
  %t3_g3 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 3
  %t3_v0 = load double, ptr %t3_g0, align 16
  %t3_v1 = load double, ptr %t3_g1, align 8
  %t3_v2 = load double, ptr %t3_g2, align 16
  %t3_v3 = load double, ptr %t3_g3, align 8
  %t3_h0 = fsub double %t3_v0, %t3_v1
  %t3_h1 = fsub double %t3_v2, %t3_v3
  %t3_i0 = insertelement <2 x double> poison, double %t3_h0, i32 0
  %t3_i1 = insertelement <2 x double> %t3_i0, double %t3_h1, i32 1
  %t3_so = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 2
  store <2 x double> %t3_i1, ptr %t3_so, align 16
  ; ---- T6 CmpCluster ----
  %t6_g0 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 0
  %t6_g1 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 1
  %t6_g2 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 2
  %t6_g3 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 3
  %t6_v0 = load i32, ptr %t6_g0, align 16
  %t6_v1 = load i32, ptr %t6_g1, align 4
  %t6_v2 = load i32, ptr %t6_g2, align 8
  %t6_v3 = load i32, ptr %t6_g3, align 4
  %t6_c0 = icmp slt i32 %t6_v0, %t6_v1
  %t6_m0 = select i1 %t6_c0, i32 %t6_v0, i32 %t6_v1
  %t6_c1 = icmp slt i32 %t6_m0, %t6_v2
  %t6_m1 = select i1 %t6_c1, i32 %t6_m0, i32 %t6_v2
  %t6_c2 = icmp slt i32 %t6_m1, %t6_v3
  %t6_m2 = select i1 %t6_c2, i32 %t6_m1, i32 %t6_v3
  %t6_so = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 0
  store i32 %t6_m2, ptr %t6_so, align 16
  ret void
}
