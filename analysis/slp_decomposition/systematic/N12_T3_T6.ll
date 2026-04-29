@arr1 = local_unnamed_addr global [8 x float] zeroinitializer, align 16
@T3_buf = local_unnamed_addr global [4 x double] zeroinitializer, align 16
@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define float @f() {
  %p0 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 0
  %p1 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 1
  %p2 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 2
  %p3 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 3
  %p4 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 4
  %p5 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 5
  %p6 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 6
  %p7 = getelementptr inbounds [8 x float], ptr @arr1, i64 0, i64 7
  %v0 = load float, ptr %p0, align 16
  %v1 = load float, ptr %p1, align 4
  %v2 = load float, ptr %p2, align 8
  %v3 = load float, ptr %p3, align 4
  %v4 = load float, ptr %p4, align 16
  %v5 = load float, ptr %p5, align 4
  %v6 = load float, ptr %p6, align 8
  %v7 = load float, ptr %p7, align 4
  %c0 = fcmp fast ogt float %v0, %v1
  %m0 = select i1 %c0, float %v0, float %v1
  %c1 = fcmp fast ogt float %m0, %v2
  %m1 = select i1 %c1, float %m0, float %v2
  %c2 = fcmp fast ogt float %m1, %v3
  %m2 = select i1 %c2, float %m1, float %v3
  %c3 = fcmp fast ogt float %m2, %v4
  %m3 = select i1 %c3, float %m2, float %v4
  %c4 = fcmp fast ogt float %m3, %v5
  %m4 = select i1 %c4, float %m3, float %v5
  %c5 = fcmp fast ogt float %m4, %v6
  %m5 = select i1 %c5, float %m4, float %v6
  %c6 = fcmp fast ogt float %m5, %v7
  %m6 = select i1 %c6, float %m5, float %v7
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
  ret float %m6
}
