define float @f(ptr %p) {
  %v = load float, ptr %p
  ret float %v
}
