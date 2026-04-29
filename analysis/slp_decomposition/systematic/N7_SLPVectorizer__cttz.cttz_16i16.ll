target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
@src16 = common global [16 x i16] zeroinitializer, align 32
@dst16 = common global [16 x i16] zeroinitializer, align 32
declare i16 @llvm.cttz.i16(i16, i1)
define void @cttz_16i16() #0 {
  %ld0  = load i16, ptr @src16, align 2
  %ld1  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  1), align 2
  %ld2  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  2), align 2
  %ld3  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  3), align 2
  %ld4  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  4), align 2
  %ld5  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  5), align 2
  %ld6  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  6), align 2
  %ld7  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  7), align 2
  %ld8  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  8), align 2
  %ld9  = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64  9), align 2
  %ld10 = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64 10), align 2
  %ld11 = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64 11), align 2
  %ld12 = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64 12), align 2
  %ld13 = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64 13), align 2
  %ld14 = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64 14), align 2
  %ld15 = load i16, ptr getelementptr inbounds ([16 x i16], ptr @src16, i16 0, i64 15), align 2
  %cttz0  = call i16 @llvm.cttz.i16(i16 %ld0, i1 0)
  %cttz1  = call i16 @llvm.cttz.i16(i16 %ld1, i1 0)
  %cttz2  = call i16 @llvm.cttz.i16(i16 %ld2, i1 0)
  %cttz3  = call i16 @llvm.cttz.i16(i16 %ld3, i1 0)
  %cttz4  = call i16 @llvm.cttz.i16(i16 %ld4, i1 0)
  %cttz5  = call i16 @llvm.cttz.i16(i16 %ld5, i1 0)
  %cttz6  = call i16 @llvm.cttz.i16(i16 %ld6, i1 0)
  %cttz7  = call i16 @llvm.cttz.i16(i16 %ld7, i1 0)
  %cttz8  = call i16 @llvm.cttz.i16(i16 %ld8, i1 0)
  %cttz9  = call i16 @llvm.cttz.i16(i16 %ld9, i1 0)
  %cttz10 = call i16 @llvm.cttz.i16(i16 %ld10, i1 0)
  %cttz11 = call i16 @llvm.cttz.i16(i16 %ld11, i1 0)
  %cttz12 = call i16 @llvm.cttz.i16(i16 %ld12, i1 0)
  %cttz13 = call i16 @llvm.cttz.i16(i16 %ld13, i1 0)
  %cttz14 = call i16 @llvm.cttz.i16(i16 %ld14, i1 0)
  %cttz15 = call i16 @llvm.cttz.i16(i16 %ld15, i1 0)
  store i16 %cttz0 , ptr @dst16, align 2
  store i16 %cttz1 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  1), align 2
  store i16 %cttz2 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  2), align 2
  store i16 %cttz3 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  3), align 2
  store i16 %cttz4 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  4), align 2
  store i16 %cttz5 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  5), align 2
  store i16 %cttz6 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  6), align 2
  store i16 %cttz7 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  7), align 2
  store i16 %cttz8 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  8), align 2
  store i16 %cttz9 , ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64  9), align 2
  store i16 %cttz10, ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64 10), align 2
  store i16 %cttz11, ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64 11), align 2
  store i16 %cttz12, ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64 12), align 2
  store i16 %cttz13, ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64 13), align 2
  store i16 %cttz14, ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64 14), align 2
  store i16 %cttz15, ptr getelementptr inbounds ([16 x i16], ptr @dst16, i16 0, i64 15), align 2
  ret void
}
attributes #0 = { nounwind }
