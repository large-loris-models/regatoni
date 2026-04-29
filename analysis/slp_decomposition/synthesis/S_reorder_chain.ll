target triple = "x86_64-unknown-linux-gnu"
define void @reorder(ptr %a, ptr %c) {
  %a0 = getelementptr i32, ptr %a, i64 0
  %a1 = getelementptr i32, ptr %a, i64 1
  %a2 = getelementptr i32, ptr %a, i64 2
  %a3 = getelementptr i32, ptr %a, i64 3
  %v0 = load i32, ptr %a0, align 16
  %v1 = load i32, ptr %a1, align 4
  %v2 = load i32, ptr %a2, align 8
  %v3 = load i32, ptr %a3, align 4
  ; out-of-order computation
  %s3 = add i32 %v3, 7
  %s1 = add i32 %v1, 7
  %s0 = add i32 %v0, 7
  %s2 = add i32 %v2, 7
  %c0 = getelementptr i32, ptr %c, i64 0
  %c1 = getelementptr i32, ptr %c, i64 1
  %c2 = getelementptr i32, ptr %c, i64 2
  %c3 = getelementptr i32, ptr %c, i64 3
  ; out-of-order stores (3, 0, 2, 1)
  store i32 %s3, ptr %c3, align 4
  store i32 %s0, ptr %c0, align 16
  store i32 %s2, ptr %c2, align 8
  store i32 %s1, ptr %c1, align 4
  ret void
}
