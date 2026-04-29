target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
; Loads from multiple distinct base pointers feeding a single vectorizable
; computation. SLP forms a tree where the loads end up as gathered (since
; they're not contiguous on a single base) and tryToVectorizeGatheredLoads
; then attempts secondary vectorization across the per-base load groups.
define void @gathered_loads(ptr %a, ptr %b, ptr %c, ptr %d, ptr %out) #0 {
  ; 4 loads from base %a (consecutive)
  %a0 = getelementptr inbounds i32, ptr %a, i64 0
  %a1 = getelementptr inbounds i32, ptr %a, i64 1
  %a2 = getelementptr inbounds i32, ptr %a, i64 2
  %a3 = getelementptr inbounds i32, ptr %a, i64 3
  %va0 = load i32, ptr %a0, align 4
  %va1 = load i32, ptr %a1, align 4
  %va2 = load i32, ptr %a2, align 4
  %va3 = load i32, ptr %a3, align 4
  ; 4 loads from base %b (consecutive)
  %b0 = getelementptr inbounds i32, ptr %b, i64 0
  %b1 = getelementptr inbounds i32, ptr %b, i64 1
  %b2 = getelementptr inbounds i32, ptr %b, i64 2
  %b3 = getelementptr inbounds i32, ptr %b, i64 3
  %vb0 = load i32, ptr %b0, align 4
  %vb1 = load i32, ptr %b1, align 4
  %vb2 = load i32, ptr %b2, align 4
  %vb3 = load i32, ptr %b3, align 4
  ; 4 loads from base %c
  %c0 = getelementptr inbounds i32, ptr %c, i64 0
  %c1 = getelementptr inbounds i32, ptr %c, i64 1
  %c2 = getelementptr inbounds i32, ptr %c, i64 2
  %c3 = getelementptr inbounds i32, ptr %c, i64 3
  %vc0 = load i32, ptr %c0, align 4
  %vc1 = load i32, ptr %c1, align 4
  %vc2 = load i32, ptr %c2, align 4
  %vc3 = load i32, ptr %c3, align 4
  ; 4 loads from base %d
  %d0 = getelementptr inbounds i32, ptr %d, i64 0
  %d1 = getelementptr inbounds i32, ptr %d, i64 1
  %d2 = getelementptr inbounds i32, ptr %d, i64 2
  %d3 = getelementptr inbounds i32, ptr %d, i64 3
  %vd0 = load i32, ptr %d0, align 4
  %vd1 = load i32, ptr %d1, align 4
  %vd2 = load i32, ptr %d2, align 4
  %vd3 = load i32, ptr %d3, align 4
  ; cross-base interleaved compute that gathers loads from all 4 bases
  %x0 = add i32 %va0, %vb0
  %x1 = add i32 %vc1, %vd1
  %x2 = add i32 %va2, %vc2
  %x3 = add i32 %vb3, %vd3
  %y0 = mul i32 %x0, %vc0
  %y1 = mul i32 %x1, %vd2
  %y2 = mul i32 %x2, %vb1
  %y3 = mul i32 %x3, %va3
  %o0 = getelementptr inbounds i32, ptr %out, i64 0
  %o1 = getelementptr inbounds i32, ptr %out, i64 1
  %o2 = getelementptr inbounds i32, ptr %out, i64 2
  %o3 = getelementptr inbounds i32, ptr %out, i64 3
  store i32 %y0, ptr %o0, align 4
  store i32 %y1, ptr %o1, align 4
  store i32 %y2, ptr %o2, align 4
  store i32 %y3, ptr %o3, align 4
  ret void
}
attributes #0 = { "target-features"="+avx2" }
