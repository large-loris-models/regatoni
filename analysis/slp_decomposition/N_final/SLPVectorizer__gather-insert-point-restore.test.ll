declare i16 @llvm.umax.i16(i16, i16) #1
define i16 @test(ptr %i) {
;
entry:
  %gep.us154 = getelementptr i8, ptr %i, i64 132860
  %gep.us154.1 = getelementptr i8, ptr %i, i64 137774
  %gep.us154.2 = getelementptr i8, ptr %i, i64 142688
  %gep.us154.3 = getelementptr i8, ptr %i, i64 147602
  %gep.us154.4 = getelementptr i8, ptr %i, i64 152516
  %gep.us154.5 = getelementptr i8, ptr %i, i64 157430
  br label %for.cond5.us

for.cond5.us:
  %0 = load i16, ptr %gep.us154, align 2
  %1 = load i16, ptr %gep.us154.1, align 2
  %2 = load i16, ptr %gep.us154.2, align 2
  %3 = load i16, ptr %gep.us154.3, align 2
  %4 = load i16, ptr %gep.us154.4, align 2
  %5 = load i16, ptr %gep.us154.5, align 2
  %6 = call i16 @llvm.umax.i16(i16 %5, i16 0)
  %7 = call i16 @llvm.umax.i16(i16 %0, i16 %6)
  %8 = call i16 @llvm.umax.i16(i16 %1, i16 %7)
  %9 = call i16 @llvm.umax.i16(i16 %2, i16 %8)
  %10 = call i16 @llvm.umax.i16(i16 %3, i16 %9)
  %11 = call i16 @llvm.umax.i16(i16 %2, i16 %10)
  %12 = call i16 @llvm.umax.i16(i16 %3, i16 %11)
  %13 = call i16 @llvm.umax.i16(i16 %4, i16 %12)
  %14 = load i16, ptr %gep.us154, align 2
  %15 = call i16 @llvm.umax.i16(i16 %14, i16 %13)
  %16 = load i16, ptr %gep.us154.1, align 2
  %17 = call i16 @llvm.umax.i16(i16 %16, i16 %15)
  %18 = call i16 @llvm.umax.i16(i16 %4, i16 %17)
  ret i16 %18
}
