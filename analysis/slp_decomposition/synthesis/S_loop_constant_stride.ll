target triple = "x86_64-unknown-linux-gnu"
define void @stride_loop(ptr noalias %A, ptr noalias %B, i64 %n) {
entry:
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %inc, %loop ]
  %i0 = mul i64 %i, 4
  %i1 = add i64 %i0, 1
  %i2 = add i64 %i0, 2
  %i3 = add i64 %i0, 3
  %p0 = getelementptr inbounds float, ptr %A, i64 %i0
  %p1 = getelementptr inbounds float, ptr %A, i64 %i1
  %p2 = getelementptr inbounds float, ptr %A, i64 %i2
  %p3 = getelementptr inbounds float, ptr %A, i64 %i3
  %v0 = load float, ptr %p0, align 16
  %v1 = load float, ptr %p1, align 4
  %v2 = load float, ptr %p2, align 8
  %v3 = load float, ptr %p3, align 4
  %m0 = fmul float %v0, 2.0
  %m1 = fmul float %v1, 2.0
  %m2 = fmul float %v2, 2.0
  %m3 = fmul float %v3, 2.0
  %q0 = getelementptr inbounds float, ptr %B, i64 %i0
  %q1 = getelementptr inbounds float, ptr %B, i64 %i1
  %q2 = getelementptr inbounds float, ptr %B, i64 %i2
  %q3 = getelementptr inbounds float, ptr %B, i64 %i3
  store float %m0, ptr %q0, align 16
  store float %m1, ptr %q1, align 4
  store float %m2, ptr %q2, align 8
  store float %m3, ptr %q3, align 4
  %inc = add i64 %i, 1
  %ec = icmp eq i64 %inc, %n
  br i1 %ec, label %exit, label %loop
exit:
  ret void
}
