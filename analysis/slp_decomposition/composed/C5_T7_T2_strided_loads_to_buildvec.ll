define <4 x i8> @f(ptr %pl, i64 %stride) {
  %s0 = mul nsw i64 %stride, 0
  %s1 = mul nsw i64 %stride, 1
  %s2 = mul nsw i64 %stride, 2
  %s3 = mul nsw i64 %stride, 3
  %g0 = getelementptr inbounds i8, ptr %pl, i64 %s0
  %g1 = getelementptr inbounds i8, ptr %pl, i64 %s1
  %g2 = getelementptr inbounds i8, ptr %pl, i64 %s2
  %g3 = getelementptr inbounds i8, ptr %pl, i64 %s3
  %v0 = load i8, ptr %g0, align 1
  %v1 = load i8, ptr %g1, align 1
  %v2 = load i8, ptr %g2, align 1
  %v3 = load i8, ptr %g3, align 1
  %r0 = insertelement <4 x i8> poison, i8 %v0, i32 0
  %r1 = insertelement <4 x i8>     %r0, i8 %v1, i32 1
  %r2 = insertelement <4 x i8>     %r1, i8 %v2, i32 2
  %r3 = insertelement <4 x i8>     %r2, i8 %v3, i32 3
  ret <4 x i8> %r3
}
