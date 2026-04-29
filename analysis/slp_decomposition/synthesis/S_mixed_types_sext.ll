target triple = "x86_64-unknown-linux-gnu"
define void @sext_chain(ptr %a, ptr %c) {
  %a0 = getelementptr i16, ptr %a, i64 0
  %a1 = getelementptr i16, ptr %a, i64 1
  %a2 = getelementptr i16, ptr %a, i64 2
  %a3 = getelementptr i16, ptr %a, i64 3
  %v0 = load i16, ptr %a0, align 8
  %v1 = load i16, ptr %a1, align 2
  %v2 = load i16, ptr %a2, align 4
  %v3 = load i16, ptr %a3, align 2
  %s0 = sext i16 %v0 to i64
  %s1 = sext i16 %v1 to i64
  %s2 = sext i16 %v2 to i64
  %s3 = sext i16 %v3 to i64
  %m0 = mul i64 %s0, %s1
  %m1 = mul i64 %s2, %s3
  %r = add i64 %m0, %m1
  %t = trunc i64 %r to i32
  store i32 %t, ptr %c, align 4
  ret void
}
