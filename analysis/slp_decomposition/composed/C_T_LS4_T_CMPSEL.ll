define void @ls4_cmpsel(ptr %r, ptr %w, i32 %t) {
  %r1 = getelementptr inbounds i32, ptr %r, i64 1
  %r2 = getelementptr inbounds i32, ptr %r, i64 2
  %r3 = getelementptr inbounds i32, ptr %r, i64 3
  %a0 = load i32, ptr %r
  %a1 = load i32, ptr %r1
  %a2 = load i32, ptr %r2
  %a3 = load i32, ptr %r3
  %c0 = icmp slt i32 %a0, %t
  %c1 = icmp slt i32 %a1, %t
  %c2 = icmp slt i32 %a2, %t
  %c3 = icmp slt i32 %a3, %t
  %s0 = select i1 %c0, i32 %a0, i32 %t
  %s1 = select i1 %c1, i32 %a1, i32 %t
  %s2 = select i1 %c2, i32 %a2, i32 %t
  %s3 = select i1 %c3, i32 %a3, i32 %t
  %w1 = getelementptr inbounds i32, ptr %w, i64 1
  %w2 = getelementptr inbounds i32, ptr %w, i64 2
  %w3 = getelementptr inbounds i32, ptr %w, i64 3
  store i32 %s0, ptr %w
  store i32 %s1, ptr %w1
  store i32 %s2, ptr %w2
  store i32 %s3, ptr %w3
  ret void
}
