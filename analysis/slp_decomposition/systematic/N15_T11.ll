@arr = local_unnamed_addr global [4 x double] zeroinitializer, align 16
@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
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
  ret { i64, i64 } %r1
}
