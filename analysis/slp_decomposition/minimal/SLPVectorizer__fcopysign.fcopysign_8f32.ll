target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
@srcA32 = common global [16 x float] zeroinitializer, align 64
@srcB32 = common global [16 x float] zeroinitializer, align 64
@dst32 = common global [16 x float] zeroinitializer, align 64
declare float @llvm.copysign.f32(float, float)
define void @fcopysign_8f32() #0 {
  %a0 = load float, ptr @srcA32, align 4
  %a1 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 1), align 4
  %a2 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 2), align 4
  %a3 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 3), align 4
  %a4 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 4), align 4
  %a5 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 5), align 4
  %a6 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 6), align 4
  %a7 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcA32, i32 0, i64 7), align 4
  %b0 = load float, ptr @srcB32, align 4
  %b1 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 1), align 4
  %b2 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 2), align 4
  %b3 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 3), align 4
  %b4 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 4), align 4
  %b5 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 5), align 4
  %b6 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 6), align 4
  %b7 = load float, ptr getelementptr inbounds ([16 x float], ptr @srcB32, i32 0, i64 7), align 4
  %fcopysign0 = call float @llvm.copysign.f32(float %a0, float %b0)
  %fcopysign1 = call float @llvm.copysign.f32(float %a1, float %b1)
  %fcopysign2 = call float @llvm.copysign.f32(float %a2, float %b2)
  %fcopysign3 = call float @llvm.copysign.f32(float %a3, float %b3)
  %fcopysign4 = call float @llvm.copysign.f32(float %a4, float %b4)
  %fcopysign5 = call float @llvm.copysign.f32(float %a5, float %b5)
  %fcopysign6 = call float @llvm.copysign.f32(float %a6, float %b6)
  %fcopysign7 = call float @llvm.copysign.f32(float %a7, float %b7)
  store float %fcopysign0, ptr @dst32, align 4
  store float %fcopysign1, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 1), align 4
  store float %fcopysign2, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 2), align 4
  store float %fcopysign3, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 3), align 4
  store float %fcopysign4, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 4), align 4
  store float %fcopysign5, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 5), align 4
  store float %fcopysign6, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 6), align 4
  store float %fcopysign7, ptr getelementptr inbounds ([16 x float], ptr @dst32, i32 0, i64 7), align 4
  ret void
}
attributes #0 = { nounwind }
