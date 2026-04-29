define void @test() {
bb:
  %0 = extractelement <2 x ptr> zeroinitializer, i32 0
  %getelementptr6 = getelementptr i8, ptr %0, i64 872
  store double 0.000000e+00, ptr %getelementptr6, align 8
  br label %bb9

bb9:
  ret void
}
