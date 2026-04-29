define i64 @bswap_i32(ptr noalias %p, ptr noalias %p1) {
;
  %g1 = getelementptr i8, ptr %p, i32 1
  %g2 = getelementptr i8, ptr %p, i32 2
  %g3 = getelementptr i8, ptr %p, i32 3

  %t0 = load i8, ptr %p
  %t1 = load i8, ptr %g1
  %t2 = load i8, ptr %g2
  %t3 = load i8, ptr %g3

  %g11 = getelementptr i8, ptr %p1, i32 1
  %g12 = getelementptr i8, ptr %p1, i32 2
  %g13 = getelementptr i8, ptr %p1, i32 3

  %t10 = load i8, ptr %p1
  %t11 = load i8, ptr %g11
  %t12 = load i8, ptr %g12
  %t13 = load i8, ptr %g13

  %a0 = add i8 %t0, %t10
  %a1 = add i8 %t1, %t11
  %a2 = add i8 %t2, %t12
  %a3 = add i8 %t3, %t13

  %z0 = zext i8 %a0 to i64
  %z1 = zext i8 %a1 to i64
  %z2 = zext i8 %a2 to i64
  %z3 = zext i8 %a3 to i64

  %sh0 = shl nuw i64 %z0, 24
  %sh1 = shl nuw nsw i64 %z1, 16
  %sh2 = shl nuw nsw i64 %z2, 8
;  %sh3 = shl nuw nsw i64 %z3, 0 <-- missing phantom shift

  %or01 = or disjoint i64 %sh0, %sh1
  %or012 = or disjoint i64 %or01, %sh2
  %or0123 = or disjoint i64 %or012, %z3
  ret i64 %or0123
}
