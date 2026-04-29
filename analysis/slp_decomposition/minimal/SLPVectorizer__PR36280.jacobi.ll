define float @jacobi(ptr %p, float %x, float %y, float %z) {
  %gep1 = getelementptr float, ptr %p, i64 1
  %gep2 = getelementptr float, ptr %p, i64 2
  %p1 = load float, ptr %gep1
  %p2 = load float, ptr %gep2
  %mul1 = fmul float %p1, %x
  %mul2 = fmul float %p2, %y
  %add1 = fadd float %mul1, %z
  %add2 = fadd float %mul2, %add1
  ret float %add2
}
