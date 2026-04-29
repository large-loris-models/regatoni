define void @f(ptr %v, ptr %out) {
  %p0 = getelementptr double, ptr %v, i64 0
  %p1 = getelementptr double, ptr %v, i64 1
  %p2 = getelementptr double, ptr %v, i64 2
  %p3 = getelementptr double, ptr %v, i64 3
  %a0 = load double, ptr %p0, align 8
  %a1 = load double, ptr %p1, align 8
  %a2 = load double, ptr %p2, align 8
  %a3 = load double, ptr %p3, align 8
  ; T3 — adjacent-lane fsub
  %r0 = fsub double %a0, %a1
  %r1 = fsub double %a2, %a3
  ; T1 store chain
  %o0 = getelementptr double, ptr %out, i64 0
  %o1 = getelementptr double, ptr %out, i64 1
  store double %r0, ptr %o0, align 8
  store double %r1, ptr %o1, align 8
  ret void
}
