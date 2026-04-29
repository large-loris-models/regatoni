target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"
target triple = "aarch64--linux-gnu"
define void @f(ptr %a, ptr %b, ptr %c) {
  %a1 = getelementptr i64, ptr %a, i64 1
  %b1 = getelementptr i64, ptr %b, i64 1
  %c1 = getelementptr i64, ptr %c, i64 1
  %x0 = load i64, ptr %a, align 8
  %x1 = load i64, ptr %a1, align 8
  %y0 = load i64, ptr %b, align 8
  %y1 = load i64, ptr %b1, align 8
  %s0 = add i64 %x0, %y0
  %s1 = add i64 %x1, %y1
  store i64 %s0, ptr %c, align 8
  store i64 %s1, ptr %c1, align 8
  ret void
}
