@arr = local_unnamed_addr global [5 x float] zeroinitializer, align 16
define float @f() {
  %p0 = getelementptr inbounds [5 x float], ptr @arr, i64 0, i64 0
  %p1 = getelementptr inbounds [5 x float], ptr @arr, i64 0, i64 1
  %p2 = getelementptr inbounds [5 x float], ptr @arr, i64 0, i64 2
  %p3 = getelementptr inbounds [5 x float], ptr @arr, i64 0, i64 3
  %p4 = getelementptr inbounds [5 x float], ptr @arr, i64 0, i64 4
  %v0 = load float, ptr %p0, align 16
  %v1 = load float, ptr %p1, align 4
  %v2 = load float, ptr %p2, align 8
  %v3 = load float, ptr %p3, align 4
  %v4 = load float, ptr %p4, align 16
  %c0 = fcmp fast ogt float %v0, %v1
  %m0 = select i1 %c0, float %v0, float %v1
  %c1 = fcmp fast ogt float %m0, %v2
  %m1 = select i1 %c1, float %m0, float %v2
  %c2 = fcmp fast ogt float %m1, %v3
  %m2 = select i1 %c2, float %m1, float %v3
  %c3 = fcmp fast ogt float %m2, %v4
  %m3 = select i1 %c3, float %m2, float %v4
  ret float %m3
}
