@T3_buf = local_unnamed_addr global [4 x double] zeroinitializer, align 16
define void @extract_mask(ptr %object, double %conv503, double %conv520) {
;
entry:
  %0 = load ptr, ptr %object, align 8
  %bbox483 = getelementptr float, ptr %0
  %1 = load float, ptr %bbox483, align 8
  %conv486 = fpext float %1 to double
  %cmp487 = fcmp ogt double %conv486, -2.000000e+10
  %conv486.2 = select i1 %cmp487, double %conv486, double -2.000000e+10
  %arrayidx502 = getelementptr float, ptr %0, i64 1
  %2 = load float, ptr %arrayidx502, align 4
  %conv5033 = fpext float %2 to double
  %cmp504 = fcmp ogt double %conv503, 0.000000e+00
  %cond514 = select i1 %cmp504, double %conv5033, double 0.000000e+00
  %sub626 = fsub double 0.000000e+00, %conv486.2
  %conv627 = fptrunc double %sub626 to float
  %sub632 = fsub double 0.000000e+00, %cond514
  %conv633 = fptrunc double %sub632 to float
  %mul646 = fmul float %conv633, %conv627
  %cmp663 = fcmp olt float %mul646, 0.000000e+00
  br i1 %cmp663, label %if.then665, label %if.end668

if.then665:                                       ; preds = %entry
  %arrayidx656 = getelementptr float, ptr %object, i64 10
  %lengths652 = getelementptr float, ptr %object, i64 11
  %bbox651 = getelementptr float, ptr %object
  %conv621 = fptrunc double %conv520 to float
  %conv617 = fptrunc double %cond514 to float
  %conv613 = fptrunc double %conv503 to float
  store float %conv613, ptr %bbox651, align 8
  %bbox.sroa.6.0.bbox666.sroa_idx = getelementptr float, ptr %object, i64 1
  store float %conv617, ptr %bbox.sroa.6.0.bbox666.sroa_idx, align 4
  %bbox.sroa.8.0.bbox666.sroa_idx = getelementptr float, ptr %object, i64 2
  store float %conv621, ptr %bbox.sroa.8.0.bbox666.sroa_idx, align 8
  store float %conv627, ptr %lengths652, align 4
  store float %conv633, ptr %arrayidx656, align 8
  br label %if.end668

if.end668:                                        ; preds = %if.then665, %entry
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
