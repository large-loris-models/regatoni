define void @test() {
bb:
  %call = call i32 null(<2 x double> zeroinitializer)
  ret void
}
