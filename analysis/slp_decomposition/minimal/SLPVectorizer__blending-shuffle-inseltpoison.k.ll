define i8 @k(<4 x i8> %x) {
  %x0 = extractelement <4 x i8> %x, i32 0
  %x3 = extractelement <4 x i8> %x, i32 3
  %x1 = extractelement <4 x i8> %x, i32 1
  %x2 = extractelement <4 x i8> %x, i32 2
  %x0x0 = mul i8 %x0, %x0
  %x3x3 = mul i8 %x3, %x3
  %x1x1 = mul i8 %x1, %x1
  %x2x2 = mul i8 %x2, %x2
  %1 = add i8 %x0x0, %x3x3
  %2 = add i8 %x1x1, %x2x2
  %3 = sdiv i8 %1, %2
  ret i8 %3
}
