target triple = "x86_64-unknown-linux-gnu"
define i32 @bswap32(ptr %p) {
  %p1 = getelementptr i8, ptr %p, i64 1
  %p2 = getelementptr i8, ptr %p, i64 2
  %p3 = getelementptr i8, ptr %p, i64 3
  %b0 = load i8, ptr %p, align 1
  %b1 = load i8, ptr %p1, align 1
  %b2 = load i8, ptr %p2, align 1
  %b3 = load i8, ptr %p3, align 1
  %z0 = zext i8 %b0 to i32
  %z1 = zext i8 %b1 to i32
  %z2 = zext i8 %b2 to i32
  %z3 = zext i8 %b3 to i32
  %s0 = shl i32 %z0, 24
  %s1 = shl i32 %z1, 16
  %s2 = shl i32 %z2, 8
  %or0 = or i32 %s0, %s1
  %or1 = or i32 %or0, %s2
  %or2 = or i32 %or1, %z3
  ret i32 %or2
}
