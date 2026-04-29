target datalayout = "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128"
target triple = "aarch64-unknown-unknown"
@T7_buf = local_unnamed_addr global [128 x i8] zeroinitializer, align 16
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
  ; ---- T7 RuntimeStrideGEP ----
  %t7_stride = load volatile i64, ptr @T7_buf, align 8
  %t7_s0 = mul nsw i64 %t7_stride, 0
  %t7_s1 = mul nsw i64 %t7_stride, 1
  %t7_s2 = mul nsw i64 %t7_stride, 2
  %t7_s3 = mul nsw i64 %t7_stride, 3
  %t7_p0 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s0
  %t7_p1 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s1
  %t7_p2 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s2
  %t7_p3 = getelementptr inbounds i8, ptr @T7_buf, i64 %t7_s3
  %t7_v0 = load i8, ptr %t7_p0, align 1
  %t7_v1 = load i8, ptr %t7_p1, align 1
  %t7_v2 = load i8, ptr %t7_p2, align 1
  %t7_v3 = load i8, ptr %t7_p3, align 1
  %t7_o0 = getelementptr inbounds i8, ptr @T7_buf, i64 100
  %t7_o1 = getelementptr inbounds i8, ptr @T7_buf, i64 101
  %t7_o2 = getelementptr inbounds i8, ptr @T7_buf, i64 102
  %t7_o3 = getelementptr inbounds i8, ptr @T7_buf, i64 103
  store i8 %t7_v0, ptr %t7_o0, align 1
  store i8 %t7_v1, ptr %t7_o1, align 1
  store i8 %t7_v2, ptr %t7_o2, align 1
  store i8 %t7_v3, ptr %t7_o3, align 1
  ret void
}
