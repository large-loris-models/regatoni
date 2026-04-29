declare double @llvm.powi.f64(double, i32)
define <2 x double> @f(<2 x double> %a) {
  %a0 = extractelement <2 x double> %a, i32 0
  %a1 = extractelement <2 x double> %a, i32 1
  %c0 = call double @llvm.powi.f64(double %a0, i32 6)
  %c1 = call double @llvm.powi.f64(double %a1, i32 6)
  %r0 = insertelement <2 x double> poison, double %c0, i32 0
  %r1 = insertelement <2 x double>     %r0, double %c1, i32 1
  ret <2 x double> %r1
}
