define <4 x i32> @icmp_eq_v4i32(<4 x i32> %a, ptr %b) {
  %a0 = extractelement <4 x i32> %a, i32 0
  %a1 = extractelement <4 x i32> %a, i32 1
  %a2 = extractelement <4 x i32> %a, i32 2
  %a3 = extractelement <4 x i32> %a, i32 3
  %p1 = getelementptr inbounds i32, ptr %b, i32 1
  %p2 = getelementptr inbounds i32, ptr %b, i32 2
  %p3 = getelementptr inbounds i32, ptr %b, i32 3
  %b0 = load i32, ptr %b, align 4
  %b1 = load i32, ptr %p1, align 4
  %b2 = load i32, ptr %p2, align 4
  %b3 = load i32, ptr %p3, align 4
  %c0 = icmp eq i32 %a0, %b0
  %c1 = icmp eq i32 %b1, %a1
  %c2 = icmp eq i32 %b2, %a2
  %c3 = icmp eq i32 %a3, %b3
  %d0 = insertelement <4 x i1> poison, i1 %c0, i32 0
  %d1 = insertelement <4 x i1>   %d0, i1 %c1, i32 1
  %d2 = insertelement <4 x i1>   %d1, i1 %c2, i32 2
  %d3 = insertelement <4 x i1>   %d2, i1 %c3, i32 3
  %r = sext <4 x i1> %d3 to <4 x i32>
  ret <4 x i32> %r
}
