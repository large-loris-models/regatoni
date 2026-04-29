define i32 @f(i32 %a, i32 %b) {
  %x1 = xor i32 %a, %b
  %i1 = icmp eq i32 %x1, 0
  %s1 = sext i1 %i1 to i32
  %x2 = xor i32 %a, %b
  %i2 = icmp eq i32 %x2, 0
  %s2 = sext i1 %i2 to i32
  %x3 = xor i32 %a, %b
  %i3 = icmp eq i32 %x3, 0
  %s3 = sext i1 %i3 to i32
  %x4 = xor i32 %a, %b
  %i4 = icmp eq i32 %x4, 0
  %s4 = sext i1 %i4 to i32
  %r1 = add i32 %a, %s1
  %r2 = add i32 %r1, %s2
  %r3 = add i32 %r2, %s3
  %r4 = add i32 %r3, %s4
  %r5 = add i32 %r4, %x4
  ret i32 %r5
}
