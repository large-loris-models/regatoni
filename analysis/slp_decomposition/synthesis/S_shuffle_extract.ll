target triple = "x86_64-unknown-linux-gnu"
define <4 x float> @shuffle_extract(<8 x float> %v) {
  %e0 = shufflevector <8 x float> %v, <8 x float> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
  %e1 = shufflevector <8 x float> %v, <8 x float> poison, <4 x i32> <i32 4, i32 5, i32 6, i32 7>
  %s = fadd <4 x float> %e0, %e1
  ret <4 x float> %s
}
