define void @f() {
entry:
  br i1 false, label %b1, label %b2
b2:
  %c1 = fcmp ogt x86_fp80 0xK00000000000000000000, 0xK00000000000000000000
  %i1 = select i1 %c1, i64 0, i64 0
  %j1 = select i1 %c1, i64 0, i64 0
  %c2 = fcmp olt x86_fp80 0xK00000000000000000000, 0xK00000000000000000000
  %i2 = select i1 %c2, i64 0, i64 0
  %j2 = select i1 %c2, i64 0, i64 0
  br label %b1
b1:
  %p1 = phi i64 [0, %entry], [%i2, %b2]
  %p2 = phi i64 [0, %entry], [%j2, %b2]
  %p3 = phi i64 [0, %entry], [%i1, %b2]
  %p4 = phi i64 [0, %entry], [%j1, %b2]
  ret void
}
