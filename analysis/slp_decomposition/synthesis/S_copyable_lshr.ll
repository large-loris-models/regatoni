target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
; 8-wide store chain where the value stored at lane 0 is the raw scalar %a
; and lanes 1..7 are lshr(%a, k). At AVX2 SLP models the raw store as a
; copyable lshr-by-0 element. Exercises StoreChainContext::isFirstSizeSameRange
; and the InstructionsCompatibilityAnalysis copyable-element pipeline.
define void @copyable_lshr(i32 %a, ptr %out) #0 {
entry:
  store i32 %a, ptr %out, align 4
  %s1 = lshr i32 %a, 1
  %p1 = getelementptr inbounds i8, ptr %out, i64 4
  store i32 %s1, ptr %p1, align 4
  %s2 = lshr i32 %a, 2
  %p2 = getelementptr inbounds i8, ptr %out, i64 8
  store i32 %s2, ptr %p2, align 4
  %s3 = lshr i32 %a, 3
  %p3 = getelementptr inbounds i8, ptr %out, i64 12
  store i32 %s3, ptr %p3, align 4
  %s4 = lshr i32 %a, 4
  %p4 = getelementptr inbounds i8, ptr %out, i64 16
  store i32 %s4, ptr %p4, align 4
  %s5 = lshr i32 %a, 5
  %p5 = getelementptr inbounds i8, ptr %out, i64 20
  store i32 %s5, ptr %p5, align 4
  %s6 = lshr i32 %a, 6
  %p6 = getelementptr inbounds i8, ptr %out, i64 24
  store i32 %s6, ptr %p6, align 4
  %s7 = lshr i32 %a, 7
  %p7 = getelementptr inbounds i8, ptr %out, i64 28
  store i32 %s7, ptr %p7, align 4
  ret void
}
attributes #0 = { "target-features"="+avx2" }
