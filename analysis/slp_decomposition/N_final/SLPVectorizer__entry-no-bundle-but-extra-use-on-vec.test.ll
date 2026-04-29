define void @test(ptr %nExp, float %0, i1 %cmp, float %1) {
;
entry:
  br i1 %cmp, label %if.then, label %if.end

if.then:
  %div.i41 = fmul float %0, 0.000000e+00
  %2 = load float, ptr %nExp, align 4
  %div.1.i.i = fmul float %2, 0.000000e+00
  %div.2.i.i = fmul float %0, 0.000000e+00
  br label %if.end

if.end:
  %3 = phi float [ %1, %if.then ], [ %0, %entry ]
  %4 = phi float [ 0.000000e+00, %if.then ], [ %1, %entry ]
  %5 = phi float [ 0.000000e+00, %if.then ], [ 0x7FF8000000000000, %entry ]
  %6 = phi float [ 0.000000e+00, %if.then ], [ 1.000000e+00, %entry ]
  %fa.sroa.9.0 = phi float [ %div.2.i.i, %if.then ], [ 0.000000e+00, %entry ]
  %fa.sroa.7.0 = phi float [ %div.1.i.i, %if.then ], [ 0.000000e+00, %entry ]
  %fa.sroa.0.0 = phi float [ %div.i41, %if.then ], [ 0.000000e+00, %entry ]
  %mul.1.i.i58 = fmul float %fa.sroa.7.0, %6
  %mul.2.i.i60 = fmul float %fa.sroa.9.0, %6
  %mul.1.i.i.i63 = fmul float %fa.sroa.0.0, %5
  %mul.2.i.i.i65 = fmul float %fa.sroa.0.0, 0.000000e+00
  %mul.i66 = fmul float %fa.sroa.0.0, 0.000000e+00
  %add.1.i.i = fadd float %mul.1.i.i58, %mul.1.i.i.i63
  %add.2.i.i = fadd float %mul.2.i.i60, %mul.2.i.i.i65
  %mul.1.i.i74 = fmul float %add.1.i.i, 0.000000e+00
  %mul.2.i.i76 = fmul float %add.2.i.i, 0.000000e+00
  %mul.i.i.i78 = fmul float %mul.i66, 0.000000e+00
  %add.1.i.i85 = fadd float %mul.1.i.i74, 0.000000e+00
  %add.2.i.i86 = fadd float %mul.2.i.i76, 0.000000e+00
  %mul.i.i.i97 = fmul float %5, 0.000000e+00
  %mul.1.i.i.i99 = fmul float %4, 0.000000e+00
  %mul.2.i.i.i101 = fmul float %3, 0.000000e+00
  %add.i.i103 = fadd float %mul.i.i.i97, 0.000000e+00
  %add.1.i.i104 = fadd float %mul.1.i.i.i99, 0.000000e+00
  %add.2.i.i105 = fadd float %mul.2.i.i.i101, 0.000000e+00
  %add = fadd float %mul.i.i.i78, 0.000000e+00
  %add.i = fadd float %add.i.i103, 1.000000e+00
  %add.1.i = fadd float %add.1.i.i104, %add.1.i.i85
  %add.2.i = fadd float %add.2.i.i105, %add.2.i.i86
  %call25 = load volatile ptr, ptr null, align 8
  store float %add, ptr %call25, align 4
  %__trans_tmp_29.sroa.5.0.call25.sroa_idx = getelementptr i8, ptr %call25, i64 4
  store float %add.i, ptr %__trans_tmp_29.sroa.5.0.call25.sroa_idx, align 4
  %__trans_tmp_29.sroa.6.0.call25.sroa_idx = getelementptr i8, ptr %call25, i64 8
  store float %add.1.i, ptr %__trans_tmp_29.sroa.6.0.call25.sroa_idx, align 4
  %__trans_tmp_29.sroa.7.0.call25.sroa_idx = getelementptr i8, ptr %call25, i64 12
  store float %add.2.i, ptr %__trans_tmp_29.sroa.7.0.call25.sroa_idx, align 4
  ret void
}
