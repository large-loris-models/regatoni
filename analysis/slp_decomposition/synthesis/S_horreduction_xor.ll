target triple = "x86_64-unknown-linux-gnu"
@arr_i = local_unnamed_addr global [8 x i32] zeroinitializer, align 16
define i32 @xor_reduce() {
  %p0 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 0
  %p1 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 1
  %p2 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 2
  %p3 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 3
  %p4 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 4
  %p5 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 5
  %p6 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 6
  %p7 = getelementptr inbounds [8 x i32], ptr @arr_i, i64 0, i64 7
  %v0 = load i32, ptr %p0, align 16
  %v1 = load i32, ptr %p1, align 4
  %v2 = load i32, ptr %p2, align 8
  %v3 = load i32, ptr %p3, align 4
  %v4 = load i32, ptr %p4, align 16
  %v5 = load i32, ptr %p5, align 4
  %v6 = load i32, ptr %p6, align 8
  %v7 = load i32, ptr %p7, align 4
  %x0 = xor i32 %v0, %v1
  %x1 = xor i32 %x0, %v2
  %x2 = xor i32 %x1, %v3
  %x3 = xor i32 %x2, %v4
  %x4 = xor i32 %x3, %v5
  %x5 = xor i32 %x4, %v6
  %x6 = xor i32 %x5, %v7
  ret i32 %x6
}
