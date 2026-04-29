target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"
target triple = "aarch64--linux-gnu"
@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define void @f(ptr %a, ptr %b, ptr %c) {
  %a1 = getelementptr i64, ptr %a, i64 1
  %b1 = getelementptr i64, ptr %b, i64 1
  %c1 = getelementptr i64, ptr %c, i64 1
  %x0 = load i64, ptr %a, align 8
  %x1 = load i64, ptr %a1, align 8
  %y0 = load i64, ptr %b, align 8
  %y1 = load i64, ptr %b1, align 8
  %s0 = add i64 %x0, %y0
  %s1 = add i64 %x1, %y1
  store i64 %s0, ptr %c, align 8
  store i64 %s1, ptr %c1, align 8
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
