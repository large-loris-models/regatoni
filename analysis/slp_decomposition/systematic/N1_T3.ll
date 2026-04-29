target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"
target triple = "aarch64--linux-gnu"
@T3_buf = local_unnamed_addr global [4 x double] zeroinitializer, align 16
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
  ; ---- T3 HorReductionChain ----
  %t3_g0 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 0
  %t3_g1 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 1
  %t3_g2 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 2
  %t3_g3 = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 3
  %t3_v0 = load double, ptr %t3_g0, align 16
  %t3_v1 = load double, ptr %t3_g1, align 8
  %t3_v2 = load double, ptr %t3_g2, align 16
  %t3_v3 = load double, ptr %t3_g3, align 8
  %t3_h0 = fsub double %t3_v0, %t3_v1
  %t3_h1 = fsub double %t3_v2, %t3_v3
  %t3_i0 = insertelement <2 x double> poison, double %t3_h0, i32 0
  %t3_i1 = insertelement <2 x double> %t3_i0, double %t3_h1, i32 1
  %t3_so = getelementptr inbounds [4 x double], ptr @T3_buf, i64 0, i64 2
  store <2 x double> %t3_i1, ptr %t3_so, align 16
  ret void
}
