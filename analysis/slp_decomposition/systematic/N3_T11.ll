@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
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
