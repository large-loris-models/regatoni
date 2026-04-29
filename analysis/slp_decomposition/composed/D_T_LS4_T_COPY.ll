define void @ls4_copy(ptr %r, ptr %w, i32 %t) {
  %r1 = getelementptr inbounds i32, ptr %r, i64 1
  %r2 = getelementptr inbounds i32, ptr %r, i64 2
  %r3 = getelementptr inbounds i32, ptr %r, i64 3
  %a0 = load i32, ptr %r
  %a1 = load i32, ptr %r1
  %a2 = load i32, ptr %r2
  %a3 = load i32, ptr %r3
  %b0 = add i32 %a0, %t
  %b1 = add i32 %a1, %t
  %b2 = udiv i32 %a2, %t
  %b3 = add i32 %a3, %t
  %w1 = getelementptr inbounds i32, ptr %w, i64 1
  %w2 = getelementptr inbounds i32, ptr %w, i64 2
  %w3 = getelementptr inbounds i32, ptr %w, i64 3
  store i32 %b0, ptr %w
  store i32 %b1, ptr %w1
  store i32 %b2, ptr %w2
  store i32 %b3, ptr %w3
  ret void
}
