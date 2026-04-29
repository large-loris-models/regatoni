@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define i32 @f(ptr %p) {
entry:
  %v = load <2 x i32>, ptr %p, align 8
  %e0 = extractelement <2 x i32> %v, i32 1
  %e1 = extractelement <2 x i32> %v, i32 0
  br i1 false, label %b1, label %b3
b1:
  br i1 false, label %b2, label %b3
b2:
  br label %b3
b3:
  %a = phi i32 [ %e0, %b1 ], [ %e0, %b2 ], [ %e1, %entry ]
  %b = phi i32 [ %e1, %b1 ], [ %e1, %b2 ], [ %e0, %entry ]
  %m = mul i32 %a, %b
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
  ret i32 %m
}
