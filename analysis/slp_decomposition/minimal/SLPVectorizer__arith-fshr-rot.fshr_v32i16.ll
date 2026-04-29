@a16 = common global [32 x i16] zeroinitializer, align 64
@b16 = common global [32 x i16] zeroinitializer, align 64
@d16 = common global [32 x i16] zeroinitializer, align 64
declare i16 @llvm.fshr.i16(i16, i16, i16)
define void @fshr_v32i16() {
  %a16 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 16), align 2
  %a17 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 17), align 2
  %a18 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 18), align 2
  %a19 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 19), align 2
  %a20 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 20), align 2
  %a21 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 21), align 2
  %a22 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 22), align 2
  %a23 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 23), align 2
  %a24 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 24), align 2
  %a25 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 25), align 2
  %a26 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 26), align 2
  %a27 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 27), align 2
  %a28 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 28), align 2
  %a29 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 29), align 2
  %a30 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 30), align 2
  %a31 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @a16, i32 0, i64 31), align 2
  %b16 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 16), align 2
  %b17 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 17), align 2
  %b18 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 18), align 2
  %b19 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 19), align 2
  %b20 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 20), align 2
  %b21 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 21), align 2
  %b22 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 22), align 2
  %b23 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 23), align 2
  %b24 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 24), align 2
  %b25 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 25), align 2
  %b26 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 26), align 2
  %b27 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 27), align 2
  %b28 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 28), align 2
  %b29 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 29), align 2
  %b30 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @b16, i32 0, i64 30), align 2
  %r16 = call i16 @llvm.fshr.i16(i16 %a16, i16 %a16, i16 %b16)
  %r17 = call i16 @llvm.fshr.i16(i16 %a17, i16 %a17, i16 %b17)
  %r18 = call i16 @llvm.fshr.i16(i16 %a18, i16 %a18, i16 %b18)
  %r19 = call i16 @llvm.fshr.i16(i16 %a19, i16 %a19, i16 %b19)
  %r20 = call i16 @llvm.fshr.i16(i16 %a20, i16 %a20, i16 %b20)
  %r21 = call i16 @llvm.fshr.i16(i16 %a21, i16 %a21, i16 %b21)
  %r22 = call i16 @llvm.fshr.i16(i16 %a22, i16 %a22, i16 %b22)
  %r23 = call i16 @llvm.fshr.i16(i16 %a23, i16 %a23, i16 %b23)
  %r24 = call i16 @llvm.fshr.i16(i16 %a24, i16 %a24, i16 %b24)
  %r25 = call i16 @llvm.fshr.i16(i16 %a25, i16 %a25, i16 %b25)
  %r26 = call i16 @llvm.fshr.i16(i16 %a26, i16 %a26, i16 %b26)
  %r27 = call i16 @llvm.fshr.i16(i16 %a27, i16 %a27, i16 %b27)
  %r28 = call i16 @llvm.fshr.i16(i16 %a28, i16 %a28, i16 %b28)
  %r29 = call i16 @llvm.fshr.i16(i16 %a29, i16 %a29, i16 %b29)
  %r30 = call i16 @llvm.fshr.i16(i16 %a30, i16 %a30, i16 %b30)
  store i16 %r16, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 16), align 2
  store i16 %r17, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 17), align 2
  store i16 %r18, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 18), align 2
  store i16 %r19, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 19), align 2
  store i16 %r20, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 20), align 2
  store i16 %r21, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 21), align 2
  store i16 %r22, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 22), align 2
  store i16 %r23, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 23), align 2
  store i16 %r24, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 24), align 2
  store i16 %r25, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 25), align 2
  store i16 %r26, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 26), align 2
  store i16 %r27, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 27), align 2
  store i16 %r28, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 28), align 2
  store i16 %r29, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 29), align 2
  store i16 %r30, ptr getelementptr inbounds ([32 x i16], ptr @d16, i32 0, i64 30), align 2
  ret void
}
