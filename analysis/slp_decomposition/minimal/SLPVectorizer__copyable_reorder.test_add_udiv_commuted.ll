define void @test_add_udiv_commuted(ptr %arr1, ptr %arr2, i32 %a0, i32 %a1, i32 %a2, i32 %a3) {
entry:
  %gep1.1 = getelementptr i32, ptr %arr1, i32 1
  %gep1.2 = getelementptr i32, ptr %arr1, i32 2
  %gep1.3 = getelementptr i32, ptr %arr1, i32 3
  %gep2.1 = getelementptr i32, ptr %arr2, i32 1
  %gep2.2 = getelementptr i32, ptr %arr2, i32 2
  %gep2.3 = getelementptr i32, ptr %arr2, i32 3
  %v0 = load i32, ptr %arr1
  %v1 = load i32, ptr %gep1.1
  %v2 = load i32, ptr %gep1.2
  %v3 = load i32, ptr %gep1.3
  %y0 = add nsw i32 %a0, 1146
  %y1 = add nsw i32 %a1, 146
  %y2 = add nsw i32 %a2, 42
  %y3 = add nsw i32 %a3, 0
  %res0 = add nsw i32 %y0, %v0
  %res1 = add nsw i32 %y1, %v1
  %res2 = udiv i32 %v2, %y2
  %res3 = add nsw i32 %y3, %v3
  store i32 %res0, ptr %arr2
  store i32 %res1, ptr %gep2.1
  store i32 %res2, ptr %gep2.2
  store i32 %res3, ptr %gep2.3
  ret void
}
