define <2 x double> @f(<2 x double> %a, <2 x double> %b) {
  %a0 = extractelement <2 x double> %a, i32 0
  %a1 = extractelement <2 x double> %a, i32 1
  %b0 = extractelement <2 x double> %b, i32 0
  %b1 = extractelement <2 x double> %b, i32 1
  %r0 = fsub double %a0, %a1
  %r1 = fsub double %b0, %b1
  %i0 = insertelement <2 x double> poison, double %r0, i32 0
  %i1 = insertelement <2 x double>     %i0, double %r1, i32 1
  ret <2 x double> %i1
}
