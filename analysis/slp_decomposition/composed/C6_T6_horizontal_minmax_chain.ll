@arr = local_unnamed_addr global [4 x i64] zeroinitializer, align 16
define i64 @f() {
  %p0 = getelementptr inbounds [4 x i64], ptr @arr, i64 0, i64 0
  %p1 = getelementptr inbounds [4 x i64], ptr @arr, i64 0, i64 1
  %p2 = getelementptr inbounds [4 x i64], ptr @arr, i64 0, i64 2
  %p3 = getelementptr inbounds [4 x i64], ptr @arr, i64 0, i64 3
  %v0 = load i64, ptr %p0, align 16
  %v1 = load i64, ptr %p1, align 8
  %v2 = load i64, ptr %p2, align 8
  %v3 = load i64, ptr %p3, align 8
  %c0 = icmp slt i64 %v0, %v1
  %m0 = select i1 %c0, i64 %v0, i64 %v1
  %c1 = icmp slt i64 %m0, %v2
  %m1 = select i1 %c1, i64 %m0, i64 %v2
  %c2 = icmp slt i64 %m1, %v3
  %m2 = select i1 %c2, i64 %m1, i64 %v3
  ret i64 %m2
}
