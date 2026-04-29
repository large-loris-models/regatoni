define void @test(ptr noalias %0, ptr %p) {
;
  %2 = getelementptr inbounds float, ptr %p, i64 2
  %3 = getelementptr inbounds float, ptr %p, i64 4
  %4 = load float, ptr %3, align 4
  %5 = getelementptr inbounds float, ptr %p, i64 5
  %6 = load float, ptr %5, align 16
  %7 = getelementptr inbounds float, ptr %p, i64 15
  %8 = load float, ptr %7, align 4
  %9 = fadd reassoc nsz arcp contract afn float %8, 0.000000e+00
  %10 = getelementptr inbounds float, ptr %0, i64 2
  store float %9, ptr %10, align 4
  %11 = fadd reassoc nsz arcp contract afn float %4, 0.000000e+00
  %12 = getelementptr inbounds float, ptr %0, i64 3
  store float %11, ptr %12, align 4
  %13 = fadd reassoc nsz arcp contract afn float %6, 0.000000e+00
  %14 = getelementptr inbounds float, ptr %0, i64 4
  store float %13, ptr %14, align 4
  %15 = fadd reassoc nsz arcp contract afn float %8, 0.000000e+00
  %16 = getelementptr inbounds float, ptr %0, i64 5
  store float %15, ptr %16, align 4
  %17 = fadd reassoc nsz arcp contract afn float %4, 0.000000e+00
  %18 = load float, ptr %p, align 16
  %19 = getelementptr inbounds float, ptr %0, i64 6
  store float %17, ptr %19, align 4
  %20 = fadd reassoc nsz arcp contract afn float %6, 0.000000e+00
  %21 = getelementptr inbounds float, ptr %0, i64 7
  store float %20, ptr %21, align 4
  %22 = fadd reassoc nsz arcp contract afn float %8, 0.000000e+00
  %23 = load float, ptr %2, align 8
  %24 = getelementptr inbounds float, ptr %0, i64 8
  store float %22, ptr %24, align 4
  %25 = fadd reassoc nsz arcp contract afn float %4, %18
  %26 = getelementptr inbounds float, ptr %0, i64 9
  store float %25, ptr %26, align 4
  %27 = fadd reassoc nsz arcp contract afn float %6, 0.000000e+00
  %28 = getelementptr inbounds float, ptr %0, i64 10
  store float %27, ptr %28, align 4
  %29 = fadd reassoc nsz arcp contract afn float %8, %23
  %30 = getelementptr inbounds float, ptr %0, i64 11
  store float %29, ptr %30, align 4
  %31 = getelementptr inbounds float, ptr %p, i64 6
  %32 = load float, ptr %31, align 4
  %33 = fadd reassoc nsz arcp contract afn float %32, 0.000000e+00
  %34 = getelementptr inbounds float, ptr %0, i64 12
  store float %33, ptr %34, align 4
  %35 = getelementptr inbounds float, ptr %p, i64 7
  %36 = load float, ptr %35, align 8
  %37 = fadd reassoc nsz arcp contract afn float %36, 0.000000e+00
  %38 = getelementptr inbounds float, ptr %0, i64 13
  store float %37, ptr %38, align 4
  %39 = getelementptr inbounds float, ptr %p, i64 8
  %40 = load float, ptr %39, align 4
  %41 = fadd reassoc nsz arcp contract afn float %40, 0.000000e+00
  %42 = getelementptr inbounds float, ptr %0, i64 14
  store float %41, ptr %42, align 4
  %43 = fadd reassoc nsz arcp contract afn float %32, 0.000000e+00
  %44 = getelementptr inbounds float, ptr %0, i64 15
  store float %43, ptr %44, align 4
  %45 = fadd reassoc nsz arcp contract afn float %36, 0.000000e+00
  %46 = getelementptr inbounds float, ptr %0, i64 16
  store float %45, ptr %46, align 4
  %47 = fadd reassoc nsz arcp contract afn float %40, 0.000000e+00
  %48 = getelementptr inbounds float, ptr %0, i64 17
  store float %47, ptr %48, align 4
  ret void
}
