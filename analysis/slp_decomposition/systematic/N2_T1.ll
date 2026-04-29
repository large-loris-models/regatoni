@T1_buf = local_unnamed_addr global [8 x i64] zeroinitializer, align 16
define <2 x double> @f(<2 x double> %a, <2 x double> %b) {
  %a0 = extractelement <2 x double> %a, i32 0
  %a1 = extractelement <2 x double> %a, i32 1
  %b0 = extractelement <2 x double> %b, i32 0
  %b1 = extractelement <2 x double> %b, i32 1
  %r0 = fsub double %a0, %a1
  %r1 = fsub double %b0, %b1
  %i0 = insertelement <2 x double> poison, double %r0, i32 0
  %i1 = insertelement <2 x double>     %i0, double %r1, i32 1
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
  ret <2 x double> %i1
}
