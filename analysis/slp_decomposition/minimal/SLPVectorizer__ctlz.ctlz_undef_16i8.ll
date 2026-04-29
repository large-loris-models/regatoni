target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
@src8  = common global [32 x i8] zeroinitializer, align 32
@dst8  = common global [32 x i8] zeroinitializer, align 32
declare  i8 @llvm.ctlz.i8(i8, i1)
define void @ctlz_undef_16i8() #0 {
  %ld0  = load i8, ptr @src8, align 1
  %ld1  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  1), align 1
  %ld2  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  2), align 1
  %ld3  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  3), align 1
  %ld4  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  4), align 1
  %ld5  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  5), align 1
  %ld6  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  6), align 1
  %ld7  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  7), align 1
  %ld8  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  8), align 1
  %ld9  = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64  9), align 1
  %ld10 = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64 10), align 1
  %ld11 = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64 11), align 1
  %ld12 = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64 12), align 1
  %ld13 = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64 13), align 1
  %ld14 = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64 14), align 1
  %ld15 = load i8, ptr getelementptr inbounds ([32 x i8], ptr @src8, i8 0, i64 15), align 1
  %ctlz0  = call i8 @llvm.ctlz.i8(i8 %ld0, i1 -1)
  %ctlz1  = call i8 @llvm.ctlz.i8(i8 %ld1, i1 -1)
  %ctlz2  = call i8 @llvm.ctlz.i8(i8 %ld2, i1 -1)
  %ctlz3  = call i8 @llvm.ctlz.i8(i8 %ld3, i1 -1)
  %ctlz4  = call i8 @llvm.ctlz.i8(i8 %ld4, i1 -1)
  %ctlz5  = call i8 @llvm.ctlz.i8(i8 %ld5, i1 -1)
  %ctlz6  = call i8 @llvm.ctlz.i8(i8 %ld6, i1 -1)
  %ctlz7  = call i8 @llvm.ctlz.i8(i8 %ld7, i1 -1)
  %ctlz8  = call i8 @llvm.ctlz.i8(i8 %ld8, i1 -1)
  %ctlz9  = call i8 @llvm.ctlz.i8(i8 %ld9, i1 -1)
  %ctlz10 = call i8 @llvm.ctlz.i8(i8 %ld10, i1 -1)
  %ctlz11 = call i8 @llvm.ctlz.i8(i8 %ld11, i1 -1)
  %ctlz12 = call i8 @llvm.ctlz.i8(i8 %ld12, i1 -1)
  %ctlz13 = call i8 @llvm.ctlz.i8(i8 %ld13, i1 -1)
  %ctlz14 = call i8 @llvm.ctlz.i8(i8 %ld14, i1 -1)
  %ctlz15 = call i8 @llvm.ctlz.i8(i8 %ld15, i1 -1)
  store i8 %ctlz0 , ptr @dst8, align 1
  store i8 %ctlz1 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  1), align 1
  store i8 %ctlz2 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  2), align 1
  store i8 %ctlz3 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  3), align 1
  store i8 %ctlz4 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  4), align 1
  store i8 %ctlz5 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  5), align 1
  store i8 %ctlz6 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  6), align 1
  store i8 %ctlz7 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  7), align 1
  store i8 %ctlz8 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  8), align 1
  store i8 %ctlz9 , ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64  9), align 1
  store i8 %ctlz10, ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64 10), align 1
  store i8 %ctlz11, ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64 11), align 1
  store i8 %ctlz12, ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64 12), align 1
  store i8 %ctlz13, ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64 13), align 1
  store i8 %ctlz14, ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64 14), align 1
  store i8 %ctlz15, ptr getelementptr inbounds ([32 x i8], ptr @dst8, i8 0, i64 15), align 1
  ret void
}
attributes #0 = { nounwind }
