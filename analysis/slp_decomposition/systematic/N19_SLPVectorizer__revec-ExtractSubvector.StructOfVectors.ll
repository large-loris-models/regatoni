define { <2 x float>, <2 x float> } @StructOfVectors(ptr %Ptr) {
;
  %L0 = load float, ptr %Ptr
  %GEP1 = getelementptr inbounds float, ptr %Ptr, i64 1
  %L1 = load float, ptr %GEP1
  %GEP2 = getelementptr inbounds float, ptr %Ptr, i64 2
  %L2 = load float, ptr %GEP2
  %GEP3 = getelementptr inbounds float, ptr %Ptr, i64 3
  %L3 = load float, ptr %GEP3

  %Fadd0 = fadd fast float %L0, 1.1e+01
  %Fadd1 = fadd fast float %L1, 1.2e+01
  %Fadd2 = fadd fast float %L2, 1.3e+01
  %Fadd3 = fadd fast float %L3, 1.4e+01

  %VecIn0 = insertelement <2 x float> poison, float %Fadd0, i64 0
  %VecIn1 = insertelement <2 x float> %VecIn0, float %Fadd1, i64 1

  %VecIn2 = insertelement <2 x float> poison, float %Fadd2, i64 0
  %VecIn3 = insertelement <2 x float> %VecIn2, float %Fadd3, i64 1

  %Ret0 = insertvalue {<2 x float>, <2 x float>} poison, <2 x float> %VecIn1, 0
  %Ret1 = insertvalue {<2 x float>, <2 x float>} %Ret0, <2 x float> %VecIn3, 1
  ret {<2 x float>, <2 x float>} %Ret1
}
