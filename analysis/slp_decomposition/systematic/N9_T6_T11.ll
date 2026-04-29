@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define void @f(ptr %pl, i64 %stride, ptr %ps) {
  %s0 = mul nsw i64 %stride, 0
  %s1 = mul nsw i64 %stride, 1
  %s2 = mul nsw i64 %stride, 2
  %s3 = mul nsw i64 %stride, 3
  %g0 = getelementptr inbounds i8, ptr %pl, i64 %s0
  %g1 = getelementptr inbounds i8, ptr %pl, i64 %s1
  %g2 = getelementptr inbounds i8, ptr %pl, i64 %s2
  %g3 = getelementptr inbounds i8, ptr %pl, i64 %s3
  %v0 = load i8, ptr %g0, align 1
  %v1 = load i8, ptr %g1, align 1
  %v2 = load i8, ptr %g2, align 1
  %v3 = load i8, ptr %g3, align 1
  %t0 = getelementptr inbounds i8, ptr %ps, i64 0
  %t1 = getelementptr inbounds i8, ptr %ps, i64 1
  %t2 = getelementptr inbounds i8, ptr %ps, i64 2
  %t3 = getelementptr inbounds i8, ptr %ps, i64 3
  store i8 %v0, ptr %t0, align 1
  store i8 %v1, ptr %t1, align 1
  store i8 %v2, ptr %t2, align 1
  store i8 %v3, ptr %t3, align 1
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
