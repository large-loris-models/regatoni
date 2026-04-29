; S_R3_shl_merge_reduction: a `xor` reduction whose ReducedVals split
; into two groups — a SINGLE shl in one group and FOUR shls (with
; distinct shift constants) in the other. HorizontalReduction::
; optimizeReducedVals (lines 26999..27029 of SLPVectorizer.cpp) merges
; the size-1 group into the size-4 group via the
; InstructionsCompatibilityAnalysis Shl-copyable-elements path.
target triple = "x86_64-unknown-linux-gnu"

define i32 @shl_merge_reduce(i32 %a, i32 %b) {
entry:
  ; size-1 group: one shl
  %g0 = shl i32 %a, 1

  ; size-4 group: four shls with distinct constants
  %s1 = shl i32 %b, 2
  %s2 = shl i32 %b, 3
  %s3 = shl i32 %b, 4
  %s4 = shl i32 %b, 5

  %r0 = xor i32 %g0, %s1
  %r1 = xor i32 %r0, %s2
  %r2 = xor i32 %r1, %s3
  %r3 = xor i32 %r2, %s4
  ret i32 %r3
}
