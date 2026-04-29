target triple = "x86_64-unknown-linux-gnu"
define void @multi_ptr(ptr %a, ptr %b, ptr %c) {
  %a1 = getelementptr i32, ptr %a, i64 1
  %b1 = getelementptr i32, ptr %b, i64 1
  %c1 = getelementptr i32, ptr %c, i64 1
  %va0 = load i32, ptr %a, align 16
  %va1 = load i32, ptr %a1, align 4
  %vb0 = load i32, ptr %b, align 16
  %vb1 = load i32, ptr %b1, align 4
  %s0 = add i32 %va0, %vb0
  %s1 = add i32 %va1, %vb1
  store i32 %s0, ptr %c, align 16
  store i32 %s1, ptr %c1, align 4
  ; second cluster on different ptr
  %a2 = getelementptr i32, ptr %a, i64 4
  %a3 = getelementptr i32, ptr %a, i64 5
  %b2 = getelementptr i32, ptr %b, i64 4
  %b3 = getelementptr i32, ptr %b, i64 5
  %c2 = getelementptr i32, ptr %c, i64 4
  %c3 = getelementptr i32, ptr %c, i64 5
  %va2 = load i32, ptr %a2, align 16
  %va3 = load i32, ptr %a3, align 4
  %vb2 = load i32, ptr %b2, align 16
  %vb3 = load i32, ptr %b3, align 4
  %s2 = add i32 %va2, %vb2
  %s3 = add i32 %va3, %vb3
  store i32 %s2, ptr %c2, align 16
  store i32 %s3, ptr %c3, align 4
  ret void
}
