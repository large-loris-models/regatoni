@arr_i32 = global [32 x i32] zeroinitializer, align 16
define void @nsw_propagation_v4i32(ptr %res, i32 %start) {

  %t0 = load i32, ptr @arr_i32, align 16
  %t1 = load i32, ptr getelementptr inbounds ([32 x i32], ptr @arr_i32, i64 0, i64 1), align 4
  %t2 = load i32, ptr getelementptr inbounds ([32 x i32], ptr @arr_i32, i64 0, i64 2), align 8
  %t3 = load i32, ptr getelementptr inbounds ([32 x i32], ptr @arr_i32, i64 0, i64 3), align 4
  %s = add nsw i32 %start, %t0
  %add = add nsw i32 %t1, %s
  %add.1 = add nsw i32 %t2, %add
  %add.2 = add nsw i32 %t3, %add.1
  store i32 %add.2, ptr %res, align 16
  ret void
}
