target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-unknown"
@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
define void @f(ptr noalias %A, ptr noalias %B) {
  %b1 = getelementptr i8, ptr %B, i64 4
  %b3 = getelementptr i8, ptr %B, i64 12
  %v0 = load i32, ptr %B
  %v1 = load i32, ptr %b1
  %v3 = load i32, ptr %b3
  %d0 = udiv i32 %v0, 3
  %d1 = udiv i32 %v1, 8
  %d3 = udiv i32 %v3, 2
  %a1 = getelementptr i8, ptr %A, i64 4
  %a2 = getelementptr i8, ptr %A, i64 8
  %a3 = getelementptr i8, ptr %A, i64 12
  store i32 %d0, ptr %A
  store i32 %d1, ptr %a1
  store i32 %d0, ptr %a2          ; duplicated value
  store i32 %d3, ptr %a3
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
