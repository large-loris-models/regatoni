target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"
define i8 @PR31243_sext(i8 %v0, i8 %v1, i8 %v2, i8 %v3, ptr %ptr) {
; SSE-LABEL: @PR31243_sext(
; SSE-NEXT:  entry:
; SSE-NEXT:    [[TMP0:%.*]] = insertelement <2 x i8> poison, i8 [[V0:%.*]], i32 0
; SSE-NEXT:    [[TMP1:%.*]] = insertelement <2 x i8> [[TMP0]], i8 [[V1:%.*]], i32 1
; SSE-NEXT:    [[TMP2:%.*]] = or <2 x i8> [[TMP1]], splat (i8 1)
; SSE-NEXT:    [[TMP3:%.*]] = extractelement <2 x i8> [[TMP2]], i32 0
; SSE-NEXT:    [[TMP4:%.*]] = sext i8 [[TMP3]] to i32
; SSE-NEXT:    [[T4:%.*]] = getelementptr inbounds i8, ptr [[PTR:%.*]], i32 [[TMP4]]
; SSE-NEXT:    [[TMP5:%.*]] = extractelement <2 x i8> [[TMP2]], i32 1
; SSE-NEXT:    [[TMP6:%.*]] = sext i8 [[TMP5]] to i32
; SSE-NEXT:    [[T5:%.*]] = getelementptr inbounds i8, ptr [[PTR]], i32 [[TMP6]]
; SSE-NEXT:    [[T6:%.*]] = load i8, ptr [[T4]], align 1
; SSE-NEXT:    [[T7:%.*]] = load i8, ptr [[T5]], align 1
; SSE-NEXT:    [[T8:%.*]] = add i8 [[T6]], [[T7]]
; SSE-NEXT:    ret i8 [[T8]]
;
; AVX-LABEL: @PR31243_sext(
; AVX-NEXT:  entry:
; AVX-NEXT:    [[TMP0:%.*]] = insertelement <2 x i8> poison, i8 [[V0:%.*]], i32 0
; AVX-NEXT:    [[TMP1:%.*]] = insertelement <2 x i8> [[TMP0]], i8 [[V1:%.*]], i32 1
; AVX-NEXT:    [[TMP2:%.*]] = or <2 x i8> [[TMP1]], splat (i8 1)
; AVX-NEXT:    [[TMP3:%.*]] = extractelement <2 x i8> [[TMP2]], i32 0
; AVX-NEXT:    [[TMP4:%.*]] = sext i8 [[TMP3]] to i32
; AVX-NEXT:    [[T4:%.*]] = getelementptr inbounds i8, ptr [[PTR:%.*]], i32 [[TMP4]]
; AVX-NEXT:    [[TMP5:%.*]] = extractelement <2 x i8> [[TMP2]], i32 1
; AVX-NEXT:    [[TMP6:%.*]] = sext i8 [[TMP5]] to i32
; AVX-NEXT:    [[T5:%.*]] = getelementptr inbounds i8, ptr [[PTR]], i32 [[TMP6]]
; AVX-NEXT:    [[T6:%.*]] = load i8, ptr [[T4]], align 1
; AVX-NEXT:    [[T7:%.*]] = load i8, ptr [[T5]], align 1
; AVX-NEXT:    [[T8:%.*]] = add i8 [[T6]], [[T7]]
; AVX-NEXT:    ret i8 [[T8]]
;
entry:
  %t0 = sext i8 %v0 to i32
  %t1 = sext i8 %v1 to i32
  %t2 = or i32 %t0, 1
  %t3 = or i32 %t1, 1
  %t4 = getelementptr inbounds i8, ptr %ptr, i32 %t2
  %t5 = getelementptr inbounds i8, ptr %ptr, i32 %t3
  %t6 = load i8, ptr %t4
  %t7 = load i8, ptr %t5
  %t8 = add i8 %t6, %t7
  ret i8 %t8
}
