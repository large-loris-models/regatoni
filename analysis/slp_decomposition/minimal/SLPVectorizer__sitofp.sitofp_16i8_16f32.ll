target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
@src8  = common global [64 x i8] zeroinitializer, align 64
@dst32 = common global [16 x float] zeroinitializer, align 64
define void @sitofp_16i8_16f32() #0 {
  %ld0  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 0 ), align 64
  %ld1  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 1 ), align 1
  %ld2  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 2 ), align 2
  %ld3  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 3 ), align 1
  %ld4  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 4 ), align 4
  %ld5  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 5 ), align 1
  %ld6  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 6 ), align 2
  %ld7  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 7 ), align 1
  %ld8  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 8 ), align 8
  %ld9  = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 9 ), align 1
  %ld10 = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 10), align 2
  %ld11 = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 11), align 1
  %ld12 = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 12), align 4
  %ld13 = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 13), align 1
  %ld14 = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 14), align 2
  %ld15 = load i8, ptr getelementptr inbounds ([64 x i8], ptr @src8, i32 0, i64 15), align 1
  %cvt0  = sitofp i8 %ld0  to float
  %cvt1  = sitofp i8 %ld1  to float
  %cvt2  = sitofp i8 %ld2  to float
  %cvt3  = sitofp i8 %ld3  to float
  %cvt4  = sitofp i8 %ld4  to float
  %cvt5  = sitofp i8 %ld5  to float
  %cvt6  = sitofp i8 %ld6  to float
  %cvt7  = sitofp i8 %ld7  to float
  %cvt8  = sitofp i8 %ld8  to float
  %cvt9  = sitofp i8 %ld9  to float
  %cvt10 = sitofp i8 %ld10 to float
  %cvt11 = sitofp i8 %ld11 to float
  %cvt12 = sitofp i8 %ld12 to float
  %cvt13 = sitofp i8 %ld13 to float
  %cvt14 = sitofp i8 %ld14 to float
  %cvt15 = sitofp i8 %ld15 to float
  store float %cvt0 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 0 ), align 64
  store float %cvt1 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 1 ), align 4
  store float %cvt2 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 2 ), align 8
  store float %cvt3 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 3 ), align 4
  store float %cvt4 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 4 ), align 16
  store float %cvt5 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 5 ), align 4
  store float %cvt6 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 6 ), align 8
  store float %cvt7 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 7 ), align 4
  store float %cvt8 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 8 ), align 32
  store float %cvt9 , ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 9 ), align 4
  store float %cvt10, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 10), align 8
  store float %cvt11, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 11), align 4
  store float %cvt12, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 12), align 16
  store float %cvt13, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 13), align 4
  store float %cvt14, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 14), align 8
  store float %cvt15, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 15), align 4
  ret void
}
attributes #0 = { nounwind }
