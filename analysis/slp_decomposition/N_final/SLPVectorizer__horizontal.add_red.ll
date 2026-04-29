define i32 @add_red(ptr %A, i32 %n) {
entry:
  %cmp31 = icmp sgt i32 %n, 0
  br i1 %cmp31, label %for.body.lr.ph, label %for.end

for.body.lr.ph:
  %0 = sext i32 %n to i64
  br label %for.body

for.body:
  %i.033 = phi i64 [ 0, %for.body.lr.ph ], [ %inc, %for.body ]
  %sum.032 = phi float [ 0.000000e+00, %for.body.lr.ph ], [ %add17, %for.body ]
  %mul = shl nsw i64 %i.033, 2
  %arrayidx = getelementptr inbounds float, ptr %A, i64 %mul
  %1 = load float, ptr %arrayidx, align 4
  %mul2 = fmul float %1, 7.000000e+00
  %add28 = or disjoint i64 %mul, 1
  %arrayidx4 = getelementptr inbounds float, ptr %A, i64 %add28
  %2 = load float, ptr %arrayidx4, align 4
  %mul5 = fmul float %2, 7.000000e+00
  %add6 = fadd fast float %mul2, %mul5
  %add829 = or disjoint i64 %mul, 2
  %arrayidx9 = getelementptr inbounds float, ptr %A, i64 %add829
  %3 = load float, ptr %arrayidx9, align 4
  %mul10 = fmul float %3, 7.000000e+00
  %add11 = fadd fast float %add6, %mul10
  %add1330 = or disjoint i64 %mul, 3
  %arrayidx14 = getelementptr inbounds float, ptr %A, i64 %add1330
  %4 = load float, ptr %arrayidx14, align 4
  %mul15 = fmul float %4, 7.000000e+00
  %add16 = fadd fast float %add11, %mul15
  %add17 = fadd fast float %sum.032, %add16
  %inc = add nsw i64 %i.033, 1
  %exitcond = icmp eq i64 %inc, %0
  br i1 %exitcond, label %for.cond.for.end_crit_edge, label %for.body

for.cond.for.end_crit_edge:
  %phitmp = fptosi float %add17 to i32
  br label %for.end

for.end:
  %sum.0.lcssa = phi i32 [ %phitmp, %for.cond.for.end_crit_edge ], [ 0, %entry ]
  ret i32 %sum.0.lcssa
}
