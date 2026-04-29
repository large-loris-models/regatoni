@T6_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
@T7_buf = local_unnamed_addr global [128 x i8] zeroinitializer, align 16
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
  ; ---- T6 CmpCluster ----
  %t6_g0 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 0
  %t6_g1 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 1
  %t6_g2 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 2
  %t6_g3 = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 3
  %t6_v0 = load i32, ptr %t6_g0, align 16
  %t6_v1 = load i32, ptr %t6_g1, align 4
  %t6_v2 = load i32, ptr %t6_g2, align 8
  %t6_v3 = load i32, ptr %t6_g3, align 4
  %t6_c0 = icmp slt i32 %t6_v0, %t6_v1
  %t6_m0 = select i1 %t6_c0, i32 %t6_v0, i32 %t6_v1
  %t6_c1 = icmp slt i32 %t6_m0, %t6_v2
  %t6_m1 = select i1 %t6_c1, i32 %t6_m0, i32 %t6_v2
  %t6_c2 = icmp slt i32 %t6_m1, %t6_v3
  %t6_m2 = select i1 %t6_c2, i32 %t6_m1, i32 %t6_v3
  %t6_so = getelementptr inbounds [4 x i32], ptr @T6_buf, i64 0, i64 0
  store i32 %t6_m2, ptr %t6_so, align 16
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
