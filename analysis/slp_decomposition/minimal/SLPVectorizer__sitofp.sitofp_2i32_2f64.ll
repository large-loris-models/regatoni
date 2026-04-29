target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
@src32 = common global [16 x i32] zeroinitializer, align 64
@dst64 = common global [8 x double] zeroinitializer, align 64
define void @sitofp_2i32_2f64() #0 {
  %ld0 = load i32, ptr @src32, align 64
  %ld1 = load i32, ptr getelementptr inbounds ([16 x i32], ptr @src32, i32 0, i64 1), align 4
  %cvt0 = sitofp i32 %ld0 to double
  %cvt1 = sitofp i32 %ld1 to double
  store double %cvt0, ptr @dst64, align 64
  store double %cvt1, ptr getelementptr inbounds ([8 x double], ptr @dst64, i32 0, i64 1), align 8
  ret void
}
attributes #0 = { nounwind }
