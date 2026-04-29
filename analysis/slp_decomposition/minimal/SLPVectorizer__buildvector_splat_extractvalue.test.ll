define float @test() {
entry:
  %.obit1683 = extractvalue { i64, i1 } zeroinitializer, 1
  %state907 = or i1 %.obit1683, %.obit1683
  %state915 = or i1 false, %state907
  %state1017 = or i1 false, %state915
  %state1021 = or i1 false, %state1017
  %state1032 = or i1 %.obit1683, %state1021
  %state1035 = or i1 false, %state1032
  %state1052 = or i1 false, %state1035
  %state1067 = or i1 false, %state1052
  %state1076 = or i1 %.obit1683, %state1067
  %state1142 = or i1 false, %state1076
  %state1156 = or i1 false, %state1142
  %state1161 = or i1 false, %state1156
  %state1165 = or i1 %.obit1683, %state1161
  %state1173 = or i1 false, %state1165
  %state1193 = or i1 false, %state1173
  %state1240 = or i1 false, %state1193
  %state1258 = or i1 %.obit1683, %state1240
  %state1278 = or i1 false, %state1258
  %state1293 = or i1 false, %state1278
  %state1298 = or i1 false, %state1293
  %state1302 = or i1 %.obit1683, %state1298
  %state1329 = or i1 false, %state1302
  %state1336 = or i1 false, %state1329
  %state1350 = or i1 false, %state1336
  %state1359 = or i1 %.obit1683, %state1350
  br i1 %state1359, label %exit1, label %exit2

exit2:
  ret float 0.000000e+00

exit1:
  ret float 0.000000e+00
}
