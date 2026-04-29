define i32 @f(ptr %p) {
entry:
  %v = load <2 x i32>, ptr %p, align 8
  %e0 = extractelement <2 x i32> %v, i32 1
  %e1 = extractelement <2 x i32> %v, i32 0
  br i1 false, label %b1, label %b3
b1:
  br i1 false, label %b2, label %b3
b2:
  br label %b3
b3:
  %a = phi i32 [ %e0, %b1 ], [ %e0, %b2 ], [ %e1, %entry ]
  %b = phi i32 [ %e1, %b1 ], [ %e1, %b2 ], [ %e0, %entry ]
  %m = mul i32 %a, %b
  ret i32 %m
}
