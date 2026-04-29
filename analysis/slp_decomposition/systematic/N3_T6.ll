@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define void @f() {
entry:
  br i1 false, label %b1, label %b2
b2:
  %c1 = fcmp ogt x86_fp80 0xK00000000000000000000, 0xK00000000000000000000
  %i1 = select i1 %c1, i64 0, i64 0
  %j1 = select i1 %c1, i64 0, i64 0
  %c2 = fcmp olt x86_fp80 0xK00000000000000000000, 0xK00000000000000000000
  %i2 = select i1 %c2, i64 0, i64 0
  %j2 = select i1 %c2, i64 0, i64 0
  br label %b1
b1:
  %p1 = phi i64 [0, %entry], [%i2, %b2]
  %p2 = phi i64 [0, %entry], [%j2, %b2]
  %p3 = phi i64 [0, %entry], [%i1, %b2]
  %p4 = phi i64 [0, %entry], [%j1, %b2]
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
