define ptr @f(ptr %p, i64 %idx) {
  %r = getelementptr i32, ptr %p, i64 %idx
  ret ptr %r
}
