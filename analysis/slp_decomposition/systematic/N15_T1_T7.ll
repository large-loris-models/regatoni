@arr = local_unnamed_addr global [4 x double] zeroinitializer, align 16
@T1_buf = local_unnamed_addr global [8 x i64] zeroinitializer, align 16
@T7_buf = local_unnamed_addr global [128 x i8] zeroinitializer, align 16
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
  ret { i64, i64 } %r1
}
