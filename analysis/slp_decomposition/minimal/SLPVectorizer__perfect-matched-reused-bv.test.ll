define void @test() {
bb:
  br label %bb1

bb1:
  br i1 false, label %bb7, label %bb4

bb4:
  br i1 false, label %bb7, label %bb1

bb7:
  ret void
}
