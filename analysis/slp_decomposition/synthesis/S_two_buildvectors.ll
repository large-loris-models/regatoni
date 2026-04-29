target triple = "x86_64-unknown-linux-gnu"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
; Two separate insertelement chains in the same BB, both fed from the same
; computed scalars. Should trigger areTwoInsertFromSameBuildVector and
; isFirstInsertElement when SLP weighs whether the two inserts share a vector.
define { <4 x float>, <4 x float> } @two_buildvectors(<4 x float> %a, <4 x float> %b) #0 {
  %a0 = extractelement <4 x float> %a, i32 0
  %a1 = extractelement <4 x float> %a, i32 1
  %a2 = extractelement <4 x float> %a, i32 2
  %a3 = extractelement <4 x float> %a, i32 3
  %b0 = extractelement <4 x float> %b, i32 0
  %b1 = extractelement <4 x float> %b, i32 1
  %b2 = extractelement <4 x float> %b, i32 2
  %b3 = extractelement <4 x float> %b, i32 3
  %s0 = fadd float %a0, %b0
  %s1 = fadd float %a1, %b1
  %s2 = fadd float %a2, %b2
  %s3 = fadd float %a3, %b3
  ; First buildvector: sums in order 0,1,2,3
  %u0 = insertelement <4 x float> poison, float %s0, i32 0
  %u1 = insertelement <4 x float> %u0,    float %s1, i32 1
  %u2 = insertelement <4 x float> %u1,    float %s2, i32 2
  %u3 = insertelement <4 x float> %u2,    float %s3, i32 3
  ; Second buildvector: same scalars, different lane order (3,2,1,0)
  %v0 = insertelement <4 x float> poison, float %s3, i32 0
  %v1 = insertelement <4 x float> %v0,    float %s2, i32 1
  %v2 = insertelement <4 x float> %v1,    float %s1, i32 2
  %v3 = insertelement <4 x float> %v2,    float %s0, i32 3
  %t0 = insertvalue { <4 x float>, <4 x float> } poison, <4 x float> %u3, 0
  %t1 = insertvalue { <4 x float>, <4 x float> } %t0,   <4 x float> %v3, 1
  ret { <4 x float>, <4 x float> } %t1
}
attributes #0 = { "target-features"="+avx2" }
