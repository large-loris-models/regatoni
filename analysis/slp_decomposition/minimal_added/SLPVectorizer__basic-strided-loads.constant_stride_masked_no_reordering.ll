define void @constant_stride_masked_no_reordering(ptr %pl, i64 %stride, ptr %ps) {
;
  %gep_l0 = getelementptr inbounds i8, ptr %pl, i64 0
  %gep_l1 = getelementptr inbounds i8, ptr %pl, i64 1
  %gep_l2 = getelementptr inbounds i8, ptr %pl, i64 2
  %gep_l3 = getelementptr inbounds i8, ptr %pl, i64 3
  %gep_l4 = getelementptr inbounds i8, ptr %pl, i64 8
  %gep_l5 = getelementptr inbounds i8, ptr %pl, i64 9
  %gep_l6 = getelementptr inbounds i8, ptr %pl, i64 10
  %gep_l7 = getelementptr inbounds i8, ptr %pl, i64 11
  %gep_l8 = getelementptr inbounds i8, ptr %pl, i64 16
  %gep_l9 = getelementptr inbounds i8, ptr %pl, i64 17
  %gep_l10 = getelementptr inbounds i8, ptr %pl, i64 18
  %gep_l11 = getelementptr inbounds i8, ptr %pl, i64 19
  %gep_l12 = getelementptr inbounds i8, ptr %pl, i64 24
  %gep_l13 = getelementptr inbounds i8, ptr %pl, i64 25
  %gep_l14 = getelementptr inbounds i8, ptr %pl, i64 26
  %gep_l15 = getelementptr inbounds i8, ptr %pl, i64 27

  %load0  = load i8, ptr %gep_l0 , align 1
  %load1  = load i8, ptr %gep_l1 , align 1
  %load2  = load i8, ptr %gep_l2 , align 1
  %load3  = load i8, ptr %gep_l3 , align 1
  %load4  = load i8, ptr %gep_l4 , align 1
  %load5  = load i8, ptr %gep_l5 , align 1
  %load6  = load i8, ptr %gep_l6 , align 1
  %load7  = load i8, ptr %gep_l7 , align 1
  %load8  = load i8, ptr %gep_l8 , align 1
  %load9  = load i8, ptr %gep_l9 , align 1
  %load10 = load i8, ptr %gep_l10, align 1
  %load11 = load i8, ptr %gep_l11, align 1
  %load12 = load i8, ptr %gep_l12, align 1
  %load13 = load i8, ptr %gep_l13, align 1
  %load14 = load i8, ptr %gep_l14, align 1
  %load15 = load i8, ptr %gep_l15, align 1

  %gep_s0 = getelementptr inbounds i8, ptr %ps, i64 0
  %gep_s1 = getelementptr inbounds i8, ptr %ps, i64 1
  %gep_s2 = getelementptr inbounds i8, ptr %ps, i64 2
  %gep_s3 = getelementptr inbounds i8, ptr %ps, i64 3
  %gep_s4 = getelementptr inbounds i8, ptr %ps, i64 4
  %gep_s5 = getelementptr inbounds i8, ptr %ps, i64 5
  %gep_s6 = getelementptr inbounds i8, ptr %ps, i64 6
  %gep_s7 = getelementptr inbounds i8, ptr %ps, i64 7
  %gep_s8 = getelementptr inbounds i8, ptr %ps, i64 8
  %gep_s9 = getelementptr inbounds i8, ptr %ps, i64 9
  %gep_s10 = getelementptr inbounds i8, ptr %ps, i64 10
  %gep_s11 = getelementptr inbounds i8, ptr %ps, i64 11
  %gep_s12 = getelementptr inbounds i8, ptr %ps, i64 12
  %gep_s13 = getelementptr inbounds i8, ptr %ps, i64 13
  %gep_s14 = getelementptr inbounds i8, ptr %ps, i64 14
  %gep_s15 = getelementptr inbounds i8, ptr %ps, i64 15

  store i8 %load0, ptr %gep_s0, align 1
  store i8 %load1, ptr %gep_s1, align 1
  store i8 %load2, ptr %gep_s2, align 1
  store i8 %load3, ptr %gep_s3, align 1
  store i8 %load4, ptr %gep_s4, align 1
  store i8 %load5, ptr %gep_s5, align 1
  store i8 %load6, ptr %gep_s6, align 1
  store i8 %load7, ptr %gep_s7, align 1
  store i8 %load8, ptr %gep_s8, align 1
  store i8 %load9, ptr %gep_s9, align 1
  store i8 %load10, ptr %gep_s10, align 1
  store i8 %load11, ptr %gep_s11, align 1
  store i8 %load12, ptr %gep_s12, align 1
  store i8 %load13, ptr %gep_s13, align 1
  store i8 %load14, ptr %gep_s14, align 1
  store i8 %load15, ptr %gep_s15, align 1

  ret void
}
