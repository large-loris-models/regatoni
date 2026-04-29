@a32 = common global [16 x i32] zeroinitializer, align 64
@c32 = common global [16 x i32] zeroinitializer, align 64
define void @srem_v16i32_uniformconst() {
  %a0  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 0 ), align 4
  %a1  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 1 ), align 4
  %a2  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 2 ), align 4
  %a3  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 3 ), align 4
  %a4  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 4 ), align 4
  %a5  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 5 ), align 4
  %a6  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 6 ), align 4
  %a7  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 7 ), align 4
  %a8  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 8 ), align 4
  %a9  = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 9 ), align 4
  %a10 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 10), align 4
  %a11 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 11), align 4
  %a12 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 12), align 4
  %a13 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 13), align 4
  %a14 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 14), align 4
  %a15 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @a32, i32 0, i64 15), align 4
  %r0  = srem i32 %a0 , 5
  %r1  = srem i32 %a1 , 5
  %r2  = srem i32 %a2 , 5
  %r3  = srem i32 %a3 , 5
  %r4  = srem i32 %a4 , 5
  %r5  = srem i32 %a5 , 5
  %r6  = srem i32 %a6 , 5
  %r7  = srem i32 %a7 , 5
  %r8  = srem i32 %a8 , 5
  %r9  = srem i32 %a9 , 5
  %r10 = srem i32 %a10, 5
  %r11 = srem i32 %a11, 5
  %r12 = srem i32 %a12, 5
  %r13 = srem i32 %a13, 5
  %r14 = srem i32 %a14, 5
  %r15 = srem i32 %a15, 5
  store i32 %r0 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 0 ), align 4
  store i32 %r1 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 1 ), align 4
  store i32 %r2 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 2 ), align 4
  store i32 %r3 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 3 ), align 4
  store i32 %r4 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 4 ), align 4
  store i32 %r5 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 5 ), align 4
  store i32 %r6 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 6 ), align 4
  store i32 %r7 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 7 ), align 4
  store i32 %r8 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 8 ), align 4
  store i32 %r9 , ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 9 ), align 4
  store i32 %r10, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 10), align 4
  store i32 %r11, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 11), align 4
  store i32 %r12, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 12), align 4
  store i32 %r13, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 13), align 4
  store i32 %r14, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 14), align 4
  store i32 %r15, ptr getelementptr inbounds ([16 x i32], ptr @c32, i32 0, i64 15), align 4
  ret void
}
