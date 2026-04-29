target triple = "x86_64-unknown-linux-gnu"
define <4 x i32> @gather_shuf(<4 x i32> %a, <4 x i32> %b, ptr %p) {
  %a0 = extractelement <4 x i32> %a, i32 0
  %a2 = extractelement <4 x i32> %a, i32 2
  %b1 = extractelement <4 x i32> %b, i32 1
  %b3 = extractelement <4 x i32> %b, i32 3
  %i0 = insertelement <4 x i32> poison, i32 %a0, i32 0
  %i1 = insertelement <4 x i32>   %i0, i32 %b1, i32 1
  %i2 = insertelement <4 x i32>   %i1, i32 %a2, i32 2
  %i3 = insertelement <4 x i32>   %i2, i32 %b3, i32 3
  %s = add <4 x i32> %i3, <i32 1, i32 1, i32 1, i32 1>
  ret <4 x i32> %s
}
