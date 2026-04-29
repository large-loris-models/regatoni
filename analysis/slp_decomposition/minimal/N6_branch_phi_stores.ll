declare void @g()
define void @f(i1 %c, ptr %p, ptr %q) {
entry:
  %x0 = load i64, ptr %p
  %p1 = getelementptr i64, ptr %p, i64 1
  %x1 = load i64, ptr %p1
  br i1 %c, label %foo, label %bar
foo:
  %y0 = add i64 %x0, 1
  %y1 = add i64 %x1, 1
  br label %baz
bar:
  call void @g()
  br label %baz
baz:
  %phi0 = phi i64 [%y0, %foo], [%x0, %bar]
  %phi1 = phi i64 [%y1, %foo], [%x1, %bar]
  store i64 %phi0, ptr %q
  %q1 = getelementptr i64, ptr %q, i64 1
  store i64 %phi1, ptr %q1
  ret void
}
