target triple = "x86_64-unknown-linux-gnu"
define void @load_8wide(ptr %a, ptr %c) {
  %a1 = getelementptr i32, ptr %a, i64 1
  %a2 = getelementptr i32, ptr %a, i64 2
  %a3 = getelementptr i32, ptr %a, i64 3
  %a4 = getelementptr i32, ptr %a, i64 4
  %a5 = getelementptr i32, ptr %a, i64 5
  %a6 = getelementptr i32, ptr %a, i64 6
  %a7 = getelementptr i32, ptr %a, i64 7
  %v0 = load i32, ptr %a, align 16
  %v1 = load i32, ptr %a1, align 4
  %v2 = load i32, ptr %a2, align 8
  %v3 = load i32, ptr %a3, align 4
  %v4 = load i32, ptr %a4, align 16
  %v5 = load i32, ptr %a5, align 4
  %v6 = load i32, ptr %a6, align 8
  %v7 = load i32, ptr %a7, align 4
  %s0 = mul i32 %v0, 3
  %s1 = mul i32 %v1, 3
  %s2 = mul i32 %v2, 3
  %s3 = mul i32 %v3, 3
  %s4 = mul i32 %v4, 3
  %s5 = mul i32 %v5, 3
  %s6 = mul i32 %v6, 3
  %s7 = mul i32 %v7, 3
  %c1 = getelementptr i32, ptr %c, i64 1
  %c2 = getelementptr i32, ptr %c, i64 2
  %c3 = getelementptr i32, ptr %c, i64 3
  %c4 = getelementptr i32, ptr %c, i64 4
  %c5 = getelementptr i32, ptr %c, i64 5
  %c6 = getelementptr i32, ptr %c, i64 6
  %c7 = getelementptr i32, ptr %c, i64 7
  store i32 %s0, ptr %c, align 16
  store i32 %s1, ptr %c1, align 4
  store i32 %s2, ptr %c2, align 8
  store i32 %s3, ptr %c3, align 4
  store i32 %s4, ptr %c4, align 16
  store i32 %s5, ptr %c5, align 4
  store i32 %s6, ptr %c6, align 8
  store i32 %s7, ptr %c7, align 4
  ret void
}
