define <2 x double> @f(<2 x double> %a, <2 x double> %b, i1 %c) {
entry:
  %a0 = extractelement <2 x double> %a, i32 0
  %a1 = extractelement <2 x double> %a, i32 1
  %b0 = extractelement <2 x double> %b, i32 0
  %b1 = extractelement <2 x double> %b, i32 1
  br i1 %c, label %t, label %f_
t:
  %t0 = fadd double %a0, %b0
  %t1 = fadd double %a1, %b1
  br label %merge
f_:
  %f0 = fsub double %a0, %b0
  %f1 = fsub double %a1, %b1
  br label %merge
merge:
  %p0 = phi double [%t0, %t], [%f0, %f_]
  %p1 = phi double [%t1, %t], [%f1, %f_]
  %r0 = insertelement <2 x double> poison, double %p0, i32 0
  %r1 = insertelement <2 x double>     %r0, double %p1, i32 1
  ret <2 x double> %r1
}
