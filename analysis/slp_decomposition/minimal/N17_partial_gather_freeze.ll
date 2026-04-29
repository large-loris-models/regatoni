target triple = "x86_64-apple-macosx10.8.0"
define <4 x i32> @f(i32 %x, i32 %y) {
  %v = sdiv i32 %x, %y
  %r0 = insertelement <4 x i32> poison, i32 %v, i32 0
  %r1 = insertelement <4 x i32> %r0,    i32 %v, i32 1
  %r2 = insertelement <4 x i32> %r1,    i32 undef, i32 2
  %r3 = insertelement <4 x i32> %r2,    i32 undef, i32 3
  ret <4 x i32> %r3
}
