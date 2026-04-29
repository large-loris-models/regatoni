define void @ls2(ptr %r, ptr %w) {
  %r1 = getelementptr inbounds i32, ptr %r, i64 1
  %a0 = load i32, ptr %r
  %a1 = load i32, ptr %r1
  %b0 = add i32 %a0, 1
  %b1 = add i32 %a1, 2
  %w1 = getelementptr inbounds i32, ptr %w, i64 1
  store i32 %b0, ptr %w
  store i32 %b1, ptr %w1
  ret void
}
