define void @f(ptr %pl, i64 %stride, ptr %ps) {
  %s0 = mul nsw i64 %stride, 0
  %s1 = mul nsw i64 %stride, 1
  %s2 = mul nsw i64 %stride, 2
  %s3 = mul nsw i64 %stride, 3
  %g0 = getelementptr inbounds i8, ptr %pl, i64 %s0
  %g1 = getelementptr inbounds i8, ptr %pl, i64 %s1
  %g2 = getelementptr inbounds i8, ptr %pl, i64 %s2
  %g3 = getelementptr inbounds i8, ptr %pl, i64 %s3
  %v0 = load i8, ptr %g0, align 1
  %v1 = load i8, ptr %g1, align 1
  %v2 = load i8, ptr %g2, align 1
  %v3 = load i8, ptr %g3, align 1
  %t0 = getelementptr inbounds i8, ptr %ps, i64 0
  %t1 = getelementptr inbounds i8, ptr %ps, i64 1
  %t2 = getelementptr inbounds i8, ptr %ps, i64 2
  %t3 = getelementptr inbounds i8, ptr %ps, i64 3
  store i8 %v0, ptr %t0, align 1
  store i8 %v1, ptr %t1, align 1
  store i8 %v2, ptr %t2, align 1
  store i8 %v3, ptr %t3, align 1
  ret void
}
