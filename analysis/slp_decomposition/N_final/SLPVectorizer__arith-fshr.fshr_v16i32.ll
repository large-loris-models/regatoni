@a32 = common global [16 x i32] zeroinitializer, align 64
@b32 = common global [16 x i32] zeroinitializer, align 64
@c32 = common global [16 x i32] zeroinitializer, align 64
@d32 = common global [16 x i32] zeroinitializer, align 64
declare i32 @llvm.fshr.i32(i32, i32, i32)
define void @fshr_v16i32() {
  %a8  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 8 ), align 4
  %a9  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 9 ), align 4
  %a10 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 10), align 4
  %a11 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 11), align 4
  %a12 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 12), align 4
  %a13 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 13), align 4
  %a14 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 14), align 4
  %a15 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 15), align 4
  %b8  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 8 ), align 4
  %b9  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 9 ), align 4
  %b10 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 10), align 4
  %b11 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 11), align 4
  %b12 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 12), align 4
  %b13 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 13), align 4
  %b14 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 14), align 4
  %b15 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @b32, i32 0, i64 15), align 4
  %c8  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 8 ), align 4
  %c9  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 9 ), align 4
  %c10 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 10), align 4
  %c11 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 11), align 4
  %c12 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 12), align 4
  %c13 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 13), align 4
  %c14 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 14), align 4
  %c15 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 15), align 4
  %r8  = call i32 @llvm.fshr.i32(i32 %a8 , i32 %b8 , i32 %c8 )
  %r9  = call i32 @llvm.fshr.i32(i32 %a9 , i32 %b9 , i32 %c9 )
  %r10 = call i32 @llvm.fshr.i32(i32 %a10, i32 %b10, i32 %c10)
  %r11 = call i32 @llvm.fshr.i32(i32 %a11, i32 %b11, i32 %c11)
  %r12 = call i32 @llvm.fshr.i32(i32 %a12, i32 %b12, i32 %c12)
  %r13 = call i32 @llvm.fshr.i32(i32 %a13, i32 %b13, i32 %c13)
  %r14 = call i32 @llvm.fshr.i32(i32 %a14, i32 %b14, i32 %c14)
  %r15 = call i32 @llvm.fshr.i32(i32 %a15, i32 %b15, i32 %c15)
  store i32 %r8 , ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 8 ), align 4
  store i32 %r9 , ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 9 ), align 4
  store i32 %r10, ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 10), align 4
  store i32 %r11, ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 11), align 4
  store i32 %r12, ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 12), align 4
  store i32 %r13, ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 13), align 4
  store i32 %r14, ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 14), align 4
  store i32 %r15, ptr getelementptr inbounds ([16 x i32], ptr @d32, i32 0, i64 15), align 4
  ret void
}
