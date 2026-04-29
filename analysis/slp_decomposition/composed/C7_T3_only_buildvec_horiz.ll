define <4 x double> @f(<4 x double> %a, <4 x double> %b) {
  %a0 = extractelement <4 x double> %a, i32 0
  %a1 = extractelement <4 x double> %a, i32 1
  %a2 = extractelement <4 x double> %a, i32 2
  %a3 = extractelement <4 x double> %a, i32 3
  %b0 = extractelement <4 x double> %b, i32 0
  %b1 = extractelement <4 x double> %b, i32 1
  %b2 = extractelement <4 x double> %b, i32 2
  %b3 = extractelement <4 x double> %b, i32 3
  ; adjacent-lane horizontal sub
  %h0 = fsub double %a0, %a1
  %h1 = fsub double %a2, %a3
  %h2 = fsub double %b0, %b1
  %h3 = fsub double %b2, %b3
  %r0 = insertelement <4 x double> poison, double %h0, i32 0
  %r1 = insertelement <4 x double>     %r0, double %h1, i32 1
  %r2 = insertelement <4 x double>     %r1, double %h2, i32 2
  %r3 = insertelement <4 x double>     %r2, double %h3, i32 3
  ret <4 x double> %r3
}
