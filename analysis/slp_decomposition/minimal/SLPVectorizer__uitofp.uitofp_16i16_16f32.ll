target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
@src16 = common global [32 x i16] zeroinitializer, align 64
@dst32 = common global [16 x float] zeroinitializer, align 64
define void @uitofp_16i16_16f32() #0 {
  %ld0  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 0 ), align 64
  %ld1  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 1 ), align 2
  %ld2  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 2 ), align 4
  %ld3  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 3 ), align 2
  %ld4  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 4 ), align 8
  %ld5  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 5 ), align 2
  %ld6  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 6 ), align 4
  %ld7  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 7 ), align 2
  %ld8  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 8 ), align 16
  %ld9  = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 9 ), align 2
  %ld10 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 10), align 4
  %ld11 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 11), align 2
  %ld12 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 12), align 8
  %ld13 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 13), align 2
  %ld14 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 14), align 4
  %ld15 = load i16, ptr getelementptr inbounds ([32 x i16], ptr @src16, i32 0, i64 15), align 2
  %cvt0  = uitofp i16 %ld0  to float
  %cvt1  = uitofp i16 %ld1  to float
  %cvt2  = uitofp i16 %ld2  to float
  %cvt3  = uitofp i16 %ld3  to float
  %cvt4  = uitofp i16 %ld4  to float
  %cvt5  = uitofp i16 %ld5  to float
  %cvt6  = uitofp i16 %ld6  to float
  %cvt7  = uitofp i16 %ld7  to float
  %cvt8  = uitofp i16 %ld8  to float
  %cvt9  = uitofp i16 %ld9  to float
  %cvt10 = uitofp i16 %ld10 to float
  %cvt11 = uitofp i16 %ld11 to float
  %cvt12 = uitofp i16 %ld12 to float
  %cvt13 = uitofp i16 %ld13 to float
  %cvt14 = uitofp i16 %ld14 to float
  %cvt15 = uitofp i16 %ld15 to float
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
