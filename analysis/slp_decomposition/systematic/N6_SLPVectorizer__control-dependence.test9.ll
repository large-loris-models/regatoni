declare i64 @may_throw() willreturn
define void @test9(ptr %a, ptr %b, ptr %c) {
;
  %v1 = load i64, ptr %a
  store i64 0, ptr %a
  call i64 @may_throw()
  %a2 = getelementptr i64, ptr %a, i32 1
  %v2 = load i64, ptr %a2

  %c1 = load i64, ptr %c
  %ca2 = getelementptr i64, ptr %c, i32 1
  %c2 = load i64, ptr %ca2
  %add1 = add i64 %v1, %c1
  %add2 = add i64 %v2, %c2

  store i64 %add1, ptr %b
  %b2 = getelementptr i64, ptr %b, i32 1
  store i64 %add2, ptr %b2
  ret void
}
