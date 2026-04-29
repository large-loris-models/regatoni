@T1_buf = local_unnamed_addr global [8 x i64] zeroinitializer, align 16
@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define i32 @f(i32 %a, i32 %b) {
  %x1 = xor i32 %a, %b
  %i1 = icmp eq i32 %x1, 0
  %s1 = sext i1 %i1 to i32
  %x2 = xor i32 %a, %b
  %i2 = icmp eq i32 %x2, 0
  %s2 = sext i1 %i2 to i32
  %x3 = xor i32 %a, %b
  %i3 = icmp eq i32 %x3, 0
  %s3 = sext i1 %i3 to i32
  %x4 = xor i32 %a, %b
  %i4 = icmp eq i32 %x4, 0
  %s4 = sext i1 %i4 to i32
  %r1 = add i32 %a, %s1
  %r2 = add i32 %r1, %s2
  %r3 = add i32 %r2, %s3
  %r4 = add i32 %r3, %s4
  %r5 = add i32 %r4, %x4
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
  ret i32 %r5
}
