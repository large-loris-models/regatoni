@input1_f16 = global [9 x half] zeroinitializer, align 16
@input2_f16 = global [9 x half] zeroinitializer, align 16
@output_f16 = global [9 x half] zeroinitializer, align 16
declare half @llvm.maximumnum.f16(half, half)
define void @fmax16()  {
entry:
  %input0_0 = load half, ptr @input1_f16, align 16
  %input0_1 = load half, ptr @input2_f16, align 16
  %output0 = tail call half @llvm.maximumnum.f16(half %input0_0, half %input0_1)
  store half %output0, ptr @output_f16, align 16
  %input1_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 2), align 2
  %input1_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 2), align 2
  %output1 = tail call half @llvm.maximumnum.f16(half %input1_1, half %input1_2)
  store half %output1, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 2), align 2
  %input2_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 4), align 4
  %input2_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 4), align 4
  %output2 = tail call half @llvm.maximumnum.f16(half %input2_1, half %input2_2)
  store half %output2, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 4), align 4
  %input3_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 6), align 2
  %input3_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 6), align 2
  %output3 = tail call half @llvm.maximumnum.f16(half %input3_1, half %input3_2)
  store half %output3, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 6), align 2
  %input4_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 8), align 8
  %input4_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 8), align 8
  %output4 = tail call half @llvm.maximumnum.f16(half %input4_1, half %input4_2)
  store half %output4, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 8), align 8
  %input5_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 10), align 2
  %input5_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 10), align 2
  %output5 = tail call half @llvm.maximumnum.f16(half %input5_1, half %input5_2)
  store half %output5, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 10), align 2
  %input6_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 12), align 4
  %input6_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 12), align 4
  %output6 = tail call half @llvm.maximumnum.f16(half %input6_1, half %input6_2)
  store half %output6, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 12), align 4
  %input7_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 14), align 2
  %input7_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 14), align 2
  %output7 = tail call half @llvm.maximumnum.f16(half %input7_1, half %input7_2)
  store half %output7, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 14), align 2
  %input8_1 = load half, ptr getelementptr inbounds nuw (i8, ptr @input1_f16, i64 16), align 16
  %input8_2 = load half, ptr getelementptr inbounds nuw (i8, ptr @input2_f16, i64 16), align 16
  %output8 = tail call half @llvm.maximumnum.f16(half %input8_1, half %input8_2)
  store half %output8, ptr getelementptr inbounds nuw (i8, ptr @output_f16, i64 16), align 16
  ret void
}
