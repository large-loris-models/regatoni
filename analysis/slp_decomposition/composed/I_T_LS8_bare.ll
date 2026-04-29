define void @ls8(ptr %r, ptr %w) {
  %r1 = getelementptr inbounds i32, ptr %r, i64 1
  %r2 = getelementptr inbounds i32, ptr %r, i64 2
  %r3 = getelementptr inbounds i32, ptr %r, i64 3
  %r4 = getelementptr inbounds i32, ptr %r, i64 4
  %r5 = getelementptr inbounds i32, ptr %r, i64 5
  %r6 = getelementptr inbounds i32, ptr %r, i64 6
  %r7 = getelementptr inbounds i32, ptr %r, i64 7
  %a0 = load i32, ptr %r
  %a1 = load i32, ptr %r1
  %a2 = load i32, ptr %r2
  %a3 = load i32, ptr %r3
  %a4 = load i32, ptr %r4
  %a5 = load i32, ptr %r5
  %a6 = load i32, ptr %r6
  %a7 = load i32, ptr %r7
  %b0 = add i32 %a0, 1
  %b1 = add i32 %a1, 2
  %b2 = add i32 %a2, 3
  %b3 = add i32 %a3, 4
  %b4 = add i32 %a4, 5
  %b5 = add i32 %a5, 6
  %b6 = add i32 %a6, 7
  %b7 = add i32 %a7, 8
  %w1 = getelementptr inbounds i32, ptr %w, i64 1
  %w2 = getelementptr inbounds i32, ptr %w, i64 2
  %w3 = getelementptr inbounds i32, ptr %w, i64 3
  %w4 = getelementptr inbounds i32, ptr %w, i64 4
  %w5 = getelementptr inbounds i32, ptr %w, i64 5
  %w6 = getelementptr inbounds i32, ptr %w, i64 6
  %w7 = getelementptr inbounds i32, ptr %w, i64 7
  store i32 %b0, ptr %w
  store i32 %b1, ptr %w1
  store i32 %b2, ptr %w2
  store i32 %b3, ptr %w3
  store i32 %b4, ptr %w4
  store i32 %b5, ptr %w5
  store i32 %b6, ptr %w6
  store i32 %b7, ptr %w7
  ret void
}
