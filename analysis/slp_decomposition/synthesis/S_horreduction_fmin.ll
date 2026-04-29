target triple = "x86_64-unknown-linux-gnu"
@arr = local_unnamed_addr global [4 x float] zeroinitializer, align 16
define float @fmin_reduce() {
  %p0 = getelementptr inbounds [4 x float], ptr @arr, i64 0, i64 0
  %p1 = getelementptr inbounds [4 x float], ptr @arr, i64 0, i64 1
  %p2 = getelementptr inbounds [4 x float], ptr @arr, i64 0, i64 2
  %p3 = getelementptr inbounds [4 x float], ptr @arr, i64 0, i64 3
  %v0 = load float, ptr %p0, align 16
  %v1 = load float, ptr %p1, align 4
  %v2 = load float, ptr %p2, align 8
  %v3 = load float, ptr %p3, align 4
  %m0 = call fast float @llvm.minnum.f32(float %v0, float %v1)
  %m1 = call fast float @llvm.minnum.f32(float %m0, float %v2)
  %m2 = call fast float @llvm.minnum.f32(float %m1, float %v3)
  ret float %m2
}
declare float @llvm.minnum.f32(float, float)
