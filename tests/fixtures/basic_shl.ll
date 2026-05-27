define i32 @f(i32 %x, i32 %y) {
  %r = shl i32 %x, %y
  ret i32 %r
}
