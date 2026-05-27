define i32 @f(i32 %x, i32 %y) {
  %r = add nsw i32 %x, %y
  ret i32 %r
}
