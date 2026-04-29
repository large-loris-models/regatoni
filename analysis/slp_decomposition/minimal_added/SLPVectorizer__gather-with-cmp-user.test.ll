define i1 @test(i32 %g, i16 %d) {
;
entry:
  %0 = and i16 %d, 1
  %xor.i.i = xor i32 %g, 1
  %conv1.i.i = trunc i32 %xor.i.i to i8
  %notsub.i = add i8 %conv1.i.i, -1
  %cmp.i.i = icmp sgt i8 %notsub.i, -3
  %conv3.i.i = zext i1 %cmp.i.i to i32
  %cmp4.i.i = icmp sgt i32 %xor.i.i, %conv3.i.i
  %conv1.1.i.i = trunc i32 %g to i8
  %notsub25.i = add i8 %conv1.1.i.i, -1
  %cmp.1.i.i = icmp sgt i8 %notsub25.i, -3
  %conv3.1.i.i = zext i1 %cmp.1.i.i to i32
  %cmp4.1.i.i = icmp sgt i32 %g, %conv3.1.i.i
  %notsub26.i = add i8 %conv1.1.i.i, -9
  %cmp.i17.i = icmp sgt i8 %notsub26.i, -3
  %conv3.i18.i = zext i1 %cmp.i17.i to i32
  %cmp4.i19.i = icmp sgt i32 %g, %conv3.i18.i
  %notsub27.i = add i8 %conv1.i.i, -9
  %cmp.1.i22.i = icmp sgt i8 %notsub27.i, -3
  %conv3.1.i23.i = zext i1 %cmp.1.i22.i to i32
  %cmp4.1.i24.i = icmp sgt i32 %xor.i.i, %conv3.1.i23.i
  %1 = and i1 %cmp4.i19.i, %cmp4.1.i24.i
  %2 = and i1 %cmp4.i.i, %1
  %3 = and i1 %cmp4.1.i.i, %2
  ret i1 %3
}
