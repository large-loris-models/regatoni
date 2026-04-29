target triple = "x86_64-unknown-linux-gnu"
define i1 @or_reduce(<8 x i32> %v) {
  %v0 = extractelement <8 x i32> %v, i32 0
  %v1 = extractelement <8 x i32> %v, i32 1
  %v2 = extractelement <8 x i32> %v, i32 2
  %v3 = extractelement <8 x i32> %v, i32 3
  %v4 = extractelement <8 x i32> %v, i32 4
  %v5 = extractelement <8 x i32> %v, i32 5
  %v6 = extractelement <8 x i32> %v, i32 6
  %v7 = extractelement <8 x i32> %v, i32 7
  %c0 = icmp ne i32 %v0, 0
  %c1 = icmp ne i32 %v1, 0
  %c2 = icmp ne i32 %v2, 0
  %c3 = icmp ne i32 %v3, 0
  %c4 = icmp ne i32 %v4, 0
  %c5 = icmp ne i32 %v5, 0
  %c6 = icmp ne i32 %v6, 0
  %c7 = icmp ne i32 %v7, 0
  %o0 = or i1 %c0, %c1
  %o1 = or i1 %o0, %c2
  %o2 = or i1 %o1, %c3
  %o3 = or i1 %o2, %c4
  %o4 = or i1 %o3, %c5
  %o5 = or i1 %o4, %c6
  %o6 = or i1 %o5, %c7
  ret i1 %o6
}
