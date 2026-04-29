target triple = "x86_64-unknown-linux-gnu"

define void @mega(<8 x i32> %vi, <4 x float> %vf, ptr noalias %p, ptr noalias %q, i64 %stride, i1 %c) {
entry:
  %a0 = getelementptr i32, ptr %p, i64 0
  %a1 = getelementptr i32, ptr %p, i64 1
  %a2 = getelementptr i32, ptr %p, i64 2
  %a3 = getelementptr i32, ptr %p, i64 3
  %a4 = getelementptr i32, ptr %p, i64 4
  %a5 = getelementptr i32, ptr %p, i64 5
  %a6 = getelementptr i32, ptr %p, i64 6
  %a7 = getelementptr i32, ptr %p, i64 7
  %x0 = load i32, ptr %a0, align 16
  %x1 = load i32, ptr %a1, align 4
  %x2 = load i32, ptr %a2, align 8
  %x3 = load i32, ptr %a3, align 4
  %x4 = load i32, ptr %a4, align 16
  %x5 = load i32, ptr %a5, align 4
  %x6 = load i32, ptr %a6, align 8
  %x7 = load i32, ptr %a7, align 4
  br i1 %c, label %then, label %else
then:
  %za0 = add i32 %x0, %x4
  %zb1 = sub i32 %x1, %x5
  %za2 = add i32 %x2, %x6
  %zb3 = sub i32 %x3, %x7
  br label %merge
else:
  %za0e = sub i32 %x0, %x4
  %zb1e = add i32 %x1, %x5
  %za2e = sub i32 %x2, %x6
  %zb3e = add i32 %x3, %x7
  br label %merge
merge:
  %p0 = phi i32 [%za0, %then], [%za0e, %else]
  %p1 = phi i32 [%zb1, %then], [%zb1e, %else]
  %p2 = phi i32 [%za2, %then], [%za2e, %else]
  %p3 = phi i32 [%zb3, %then], [%zb3e, %else]
  %f0 = extractelement <4 x float> %vf, i32 0
  %f1 = extractelement <4 x float> %vf, i32 1
  %f2 = extractelement <4 x float> %vf, i32 2
  %f3 = extractelement <4 x float> %vf, i32 3
  %cmp0 = fcmp fast ogt float %f0, %f1
  %m0 = select i1 %cmp0, float %f0, float %f1
  %cmp1 = fcmp fast ogt float %m0, %f2
  %m1 = select i1 %cmp1, float %m0, float %f2
  %cmp2 = fcmp fast ogt float %m1, %f3
  %m2 = select i1 %cmp2, float %m1, float %f3
  %q0 = getelementptr i32, ptr %q, i64 0
  %q1 = getelementptr i32, ptr %q, i64 1
  %q2 = getelementptr i32, ptr %q, i64 2
  %q3 = getelementptr i32, ptr %q, i64 3
  store i32 %p0, ptr %q0, align 16
  store i32 %p1, ptr %q1, align 4
  store i32 %p2, ptr %q2, align 8
  store i32 %p3, ptr %q3, align 4
  %s0 = mul nsw i64 %stride, 0
  %s1 = mul nsw i64 %stride, 1
  %sp0 = getelementptr inbounds i8, ptr %p, i64 %s0
  %sp1 = getelementptr inbounds i8, ptr %p, i64 %s1
  %sv0 = load i8, ptr %sp0, align 1
  %sv1 = load i8, ptr %sp1, align 1
  %so0 = getelementptr inbounds i8, ptr %q, i64 100
  %so1 = getelementptr inbounds i8, ptr %q, i64 101
  store i8 %sv0, ptr %so0, align 1
  store i8 %sv1, ptr %so1, align 1
  store float %m2, ptr %q, align 4
  ret void
}
