target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
; 5-element reduction: 4 lanes vectorize and the 5th is folded in as an
; "extra reduction" via createOp(Twine, ...) — the 6-arg overload that
; isn't reached by our 4-wide reductions.
define i32 @hor_reduce5(ptr %p, i32 %extra) #0 {
  %p0 = getelementptr inbounds i32, ptr %p, i64 0
  %p1 = getelementptr inbounds i32, ptr %p, i64 1
  %p2 = getelementptr inbounds i32, ptr %p, i64 2
  %p3 = getelementptr inbounds i32, ptr %p, i64 3
  %v0 = load i32, ptr %p0, align 4
  %v1 = load i32, ptr %p1, align 4
  %v2 = load i32, ptr %p2, align 4
  %v3 = load i32, ptr %p3, align 4
  %t0 = add i32 %v0, %v1
  %t1 = add i32 %t0, %v2
  %t2 = add i32 %t1, %v3
  %t3 = add i32 %t2, %extra
  ret i32 %t3
}
attributes #0 = { "target-features"="+avx2" }
