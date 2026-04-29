declare i16 @llvm.uadd.sat.i16(i16, i16) #0
define <4 x i16> @uadd_sat_v4i16(<4 x i16> %arg0, <4 x i16> %arg1) {
bb:
  %arg0.0 = extractelement <4 x i16> %arg0, i64 0
  %arg0.1 = extractelement <4 x i16> %arg0, i64 1
  %arg0.2 = extractelement <4 x i16> %arg0, i64 2
  %arg0.3 = extractelement <4 x i16> %arg0, i64 3
  %arg1.0 = extractelement <4 x i16> %arg1, i64 0
  %arg1.1 = extractelement <4 x i16> %arg1, i64 1
  %arg1.2 = extractelement <4 x i16> %arg1, i64 2
  %arg1.3 = extractelement <4 x i16> %arg1, i64 3
  %add.0 = call i16 @llvm.uadd.sat.i16(i16 %arg0.0, i16 %arg1.0)
  %add.1 = call i16 @llvm.uadd.sat.i16(i16 %arg0.1, i16 %arg1.1)
  %add.2 = call i16 @llvm.uadd.sat.i16(i16 %arg0.2, i16 %arg1.2)
  %add.3 = call i16 @llvm.uadd.sat.i16(i16 %arg0.3, i16 %arg1.3)
  %ins.0 = insertelement <4 x i16> undef, i16 %add.0, i64 0
  %ins.1 = insertelement <4 x i16> %ins.0, i16 %add.1, i64 1
  %ins.2 = insertelement <4 x i16> %ins.1, i16 %add.2, i64 2
  %ins.3 = insertelement <4 x i16> %ins.2, i16 %add.3, i64 3
  ret <4 x i16> %ins.3
}
attributes #0 = { nounwind readnone speculatable willreturn }
