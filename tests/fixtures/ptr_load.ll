define ptr @f(ptr %p) {
  %v = load ptr, ptr %p
  ret ptr %v
}
