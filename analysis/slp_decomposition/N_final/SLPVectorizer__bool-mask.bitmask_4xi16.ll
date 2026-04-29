define i64 @bitmask_4xi16(ptr nocapture noundef readonly %src) {
; SSE-LABEL: @bitmask_4xi16(
; SSE-NEXT:  entry:
; SSE-NEXT:    [[TMP0:%.*]] = load <8 x i16>, ptr [[SRC:%.*]], align 2
; SSE-NEXT:    [[TMP1:%.*]] = icmp ne <8 x i16> [[TMP0]], zeroinitializer
; SSE-NEXT:    [[TMP2:%.*]] = bitcast <8 x i1> [[TMP1]] to i8
; SSE-NEXT:    [[OP_RDX3:%.*]] = zext i8 [[TMP2]] to i64
; SSE-NEXT:    ret i64 [[OP_RDX3]]
;
; AVX-LABEL: @bitmask_4xi16(
; AVX-NEXT:  entry:
; AVX-NEXT:    [[TMP0:%.*]] = load <8 x i16>, ptr [[SRC:%.*]], align 2
; AVX-NEXT:    [[TMP1:%.*]] = icmp ne <8 x i16> [[TMP0]], zeroinitializer
; AVX-NEXT:    [[TMP2:%.*]] = bitcast <8 x i1> [[TMP1]] to i8
; AVX-NEXT:    [[OP_RDX3:%.*]] = zext i8 [[TMP2]] to i64
; AVX-NEXT:    ret i64 [[OP_RDX3]]
;
; AVX512-LABEL: @bitmask_4xi16(
; AVX512-NEXT:  entry:
; AVX512-NEXT:    [[TMP0:%.*]] = load <8 x i16>, ptr [[SRC:%.*]], align 2
; AVX512-NEXT:    [[TMP1:%.*]] = icmp ne <8 x i16> [[TMP0]], zeroinitializer
; AVX512-NEXT:    [[TMP2:%.*]] = bitcast <8 x i1> [[TMP1]] to i8
; AVX512-NEXT:    [[OP_RDX3:%.*]] = zext i8 [[TMP2]] to i64
; AVX512-NEXT:    ret i64 [[OP_RDX3]]
;
entry:
  %0 = load i16, ptr %src, align 2
  %tobool.not = icmp ne i16 %0, 0
  %or = zext i1 %tobool.not to i64
  %arrayidx.1 = getelementptr inbounds i16, ptr %src, i64 1
  %1 = load i16, ptr %arrayidx.1, align 2
  %tobool.not.1 = icmp eq i16 %1, 0
  %or.1 = select i1 %tobool.not.1, i64 0, i64 2
  %mask.1.1 = or i64 %or.1, %or
  %arrayidx.2 = getelementptr inbounds i16, ptr %src, i64 2
  %2 = load i16, ptr %arrayidx.2, align 2
  %tobool.not.2 = icmp eq i16 %2, 0
  %or.2 = select i1 %tobool.not.2, i64 0, i64 4
  %mask.1.2 = or i64 %or.2, %mask.1.1
  %arrayidx.3 = getelementptr inbounds i16, ptr %src, i64 3
  %3 = load i16, ptr %arrayidx.3, align 2
  %tobool.not.3 = icmp eq i16 %3, 0
  %or.3 = select i1 %tobool.not.3, i64 0, i64 8
  %mask.1.3 = or i64 %or.3, %mask.1.2
  %arrayidx.4 = getelementptr inbounds i16, ptr %src, i64 4
  %4 = load i16, ptr %arrayidx.4, align 2
  %tobool.not.4 = icmp eq i16 %4, 0
  %or.4 = select i1 %tobool.not.4, i64 0, i64 16
  %mask.1.4 = or i64 %or.4, %mask.1.3
  %arrayidx.5 = getelementptr inbounds i16, ptr %src, i64 5
  %5 = load i16, ptr %arrayidx.5, align 2
  %tobool.not.5 = icmp eq i16 %5, 0
  %or.5 = select i1 %tobool.not.5, i64 0, i64 32
  %mask.1.5 = or i64 %or.5, %mask.1.4
  %arrayidx.6 = getelementptr inbounds i16, ptr %src, i64 6
  %6 = load i16, ptr %arrayidx.6, align 2
  %tobool.not.6 = icmp eq i16 %6, 0
  %or.6 = select i1 %tobool.not.6, i64 0, i64 64
  %mask.1.6 = or i64 %or.6, %mask.1.5
  %arrayidx.7 = getelementptr inbounds i16, ptr %src, i64 7
  %7 = load i16, ptr %arrayidx.7, align 2
  %tobool.not.7 = icmp eq i16 %7, 0
  %or.7 = select i1 %tobool.not.7, i64 0, i64 128
  %mask.1.7 = or i64 %or.7, %mask.1.6
  ret i64 %mask.1.7
}
