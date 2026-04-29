@arr = local_unnamed_addr global [4 x double] zeroinitializer, align 16
@T1_buf = local_unnamed_addr global [8 x i64] zeroinitializer, align 16
@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define { i64, i64 } @f(double %x) {
  %a = load double, ptr @arr, align 16
  %p1 = getelementptr inbounds [4 x double], ptr @arr, i64 0, i64 1
  %b = load double, ptr %p1, align 8
  %p2 = getelementptr inbounds [4 x double], ptr @arr, i64 0, i64 2
  %c = load double, ptr %p2, align 16
  %p3 = getelementptr inbounds [4 x double], ptr @arr, i64 0, i64 3
  %d = load double, ptr %p3, align 8
  %m0 = fmul double %a, %x
  %m1 = fmul double %b, %x
  %s0 = fadd double %m0, %c
  %s1 = fadd double %m1, %d
  %i0 = fptosi double %s0 to i64
  %i1 = fptosi double %s1 to i64
  %r0 = insertvalue { i64, i64 } undef, i64 %i0, 0
  %r1 = insertvalue { i64, i64 } %r0,   i64 %i1, 1
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
  ret { i64, i64 } %r1
}
