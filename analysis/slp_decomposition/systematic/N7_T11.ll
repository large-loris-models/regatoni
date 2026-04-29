@T11_buf = local_unnamed_addr global [4 x i32] zeroinitializer, align 16
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
  ; ---- T11 NonUniformStoreChain ----
  %t11_g0 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 0
  %t11_g1 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 1
  %t11_v0 = load i32, ptr %t11_g0, align 16
  %t11_v1 = load i32, ptr %t11_g1, align 4
  %t11_d0 = udiv i32 %t11_v0, 3
  %t11_d1 = udiv i32 %t11_v1, 8
  %t11_o0 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 0
  %t11_o1 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 1
  %t11_o2 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 2
  %t11_o3 = getelementptr inbounds [4 x i32], ptr @T11_buf, i64 0, i64 3
  store i32 %t11_d0, ptr %t11_o0, align 16
  store i32 %t11_d1, ptr %t11_o1, align 4
  store i32 %t11_d0, ptr %t11_o2, align 8
  store i32 %t11_d1, ptr %t11_o3, align 4
  ret i32 %r5
}
