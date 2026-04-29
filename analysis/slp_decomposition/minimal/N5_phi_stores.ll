define void @f(ptr %p, i1 %c) {
  %p0 = getelementptr i8, ptr %p
  %p1 = getelementptr i8, ptr %p, i32 1
  br i1 %c, label %a, label %b
a:
  br label %d
b:
  br label %d
d:
  %x = phi i8 [1, %a], [-1, %b]
  %y = phi i8 [-1, %a], [1, %b]
  store i8 %x, ptr %p0
  store i8 %y, ptr %p1
  ret void
}
