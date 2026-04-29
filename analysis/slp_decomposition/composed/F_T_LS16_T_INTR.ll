declare i32 @llvm.uadd.sat.i32(i32, i32)
@a = common global [16 x i32] zeroinitializer, align 64
@b = common global [16 x i32] zeroinitializer, align 64
@c = common global [16 x i32] zeroinitializer, align 64
define void @ls16_intr() {
  %x0 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 0), align 4
  %y0 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 0), align 4
  %r0 = call i32 @llvm.uadd.sat.i32(i32 %x0, i32 %y0)
  store i32 %r0, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 0), align 4
  %x1 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 1), align 4
  %y1 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 1), align 4
  %r1 = call i32 @llvm.uadd.sat.i32(i32 %x1, i32 %y1)
  store i32 %r1, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 1), align 4
  %x2 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 2), align 4
  %y2 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 2), align 4
  %r2 = call i32 @llvm.uadd.sat.i32(i32 %x2, i32 %y2)
  store i32 %r2, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 2), align 4
  %x3 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 3), align 4
  %y3 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 3), align 4
  %r3 = call i32 @llvm.uadd.sat.i32(i32 %x3, i32 %y3)
  store i32 %r3, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 3), align 4
  %x4 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 4), align 4
  %y4 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 4), align 4
  %r4 = call i32 @llvm.uadd.sat.i32(i32 %x4, i32 %y4)
  store i32 %r4, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 4), align 4
  %x5 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 5), align 4
  %y5 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 5), align 4
  %r5 = call i32 @llvm.uadd.sat.i32(i32 %x5, i32 %y5)
  store i32 %r5, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 5), align 4
  %x6 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 6), align 4
  %y6 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 6), align 4
  %r6 = call i32 @llvm.uadd.sat.i32(i32 %x6, i32 %y6)
  store i32 %r6, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 6), align 4
  %x7 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 7), align 4
  %y7 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 7), align 4
  %r7 = call i32 @llvm.uadd.sat.i32(i32 %x7, i32 %y7)
  store i32 %r7, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 7), align 4
  %x8 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 8), align 4
  %y8 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 8), align 4
  %r8 = call i32 @llvm.uadd.sat.i32(i32 %x8, i32 %y8)
  store i32 %r8, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 8), align 4
  %x9 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 9), align 4
  %y9 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 9), align 4
  %r9 = call i32 @llvm.uadd.sat.i32(i32 %x9, i32 %y9)
  store i32 %r9, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 9), align 4
  %x10 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 10), align 4
  %y10 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 10), align 4
  %r10 = call i32 @llvm.uadd.sat.i32(i32 %x10, i32 %y10)
  store i32 %r10, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 10), align 4
  %x11 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 11), align 4
  %y11 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 11), align 4
  %r11 = call i32 @llvm.uadd.sat.i32(i32 %x11, i32 %y11)
  store i32 %r11, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 11), align 4
  %x12 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 12), align 4
  %y12 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 12), align 4
  %r12 = call i32 @llvm.uadd.sat.i32(i32 %x12, i32 %y12)
  store i32 %r12, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 12), align 4
  %x13 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 13), align 4
  %y13 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 13), align 4
  %r13 = call i32 @llvm.uadd.sat.i32(i32 %x13, i32 %y13)
  store i32 %r13, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 13), align 4
  %x14 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 14), align 4
  %y14 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 14), align 4
  %r14 = call i32 @llvm.uadd.sat.i32(i32 %x14, i32 %y14)
  store i32 %r14, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 14), align 4
  %x15 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a, i32 0, i64 15), align 4
  %y15 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b, i32 0, i64 15), align 4
  %r15 = call i32 @llvm.uadd.sat.i32(i32 %x15, i32 %y15)
  store i32 %r15, ptr getelementptr inbounds ([16 x i32], ptr @c, i32 0, i64 15), align 4
  ret void
}
