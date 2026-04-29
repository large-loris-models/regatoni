define <4 x i64> @f(<4 x i64> %a, <4 x i64> %b) {
  %a0 = extractelement <4 x i64> %a, i32 0
  %a1 = extractelement <4 x i64> %a, i32 1
  %a2 = extractelement <4 x i64> %a, i32 2
  %a3 = extractelement <4 x i64> %a, i32 3
  %b0 = extractelement <4 x i64> %b, i32 0
  %b1 = extractelement <4 x i64> %b, i32 1
  %b2 = extractelement <4 x i64> %b, i32 2
  %b3 = extractelement <4 x i64> %b, i32 3
  %p0 = icmp slt i64 %a0, %b0
  %p1 = icmp slt i64 %a1, %b1
  %p2 = icmp slt i64 %a2, %b2
  %p3 = icmp slt i64 %a3, %b3
  %s0 = select i1 %p0, i64 %a0, i64 %b0
  %s1 = select i1 %p1, i64 %a1, i64 %b1
  %s2 = select i1 %p2, i64 %a2, i64 %b2
  %s3 = select i1 %p3, i64 %a3, i64 %b3
  %r0 = insertelement <4 x i64> poison, i64 %s0, i32 0
  %r1 = insertelement <4 x i64>     %r0, i64 %s1, i32 1
  %r2 = insertelement <4 x i64>     %r1, i64 %s2, i32 2
  %r3 = insertelement <4 x i64>     %r2, i64 %s3, i32 3
  ret <4 x i64> %r3
}
