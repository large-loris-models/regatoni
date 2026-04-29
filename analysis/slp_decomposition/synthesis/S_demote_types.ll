target triple = "x86_64-unknown-linux-gnu"
define void @demote(ptr %a, ptr %c) {
  %a1 = getelementptr i8, ptr %a, i64 1
  %a2 = getelementptr i8, ptr %a, i64 2
  %a3 = getelementptr i8, ptr %a, i64 3
  %v0 = load i8, ptr %a, align 1
  %v1 = load i8, ptr %a1, align 1
  %v2 = load i8, ptr %a2, align 1
  %v3 = load i8, ptr %a3, align 1
  %z0 = zext i8 %v0 to i32
  %z1 = zext i8 %v1 to i32
  %z2 = zext i8 %v2 to i32
  %z3 = zext i8 %v3 to i32
  %s0 = add i32 %z0, %z1
  %s1 = add i32 %z2, %z3
  %s2 = add i32 %s0, %s1
  %t0 = trunc i32 %s2 to i8
  store i8 %t0, ptr %c, align 1
  ret void
}
