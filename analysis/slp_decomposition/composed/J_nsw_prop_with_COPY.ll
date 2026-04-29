define void @prop_copy(ptr %A, ptr %B) {
  %ai1 = getelementptr inbounds i32, ptr %A, i64 1
  %ai2 = getelementptr inbounds i32, ptr %A, i64 2
  %ai3 = getelementptr inbounds i32, ptr %A, i64 3
  %bi1 = getelementptr inbounds i32, ptr %B, i64 1
  %bi2 = getelementptr inbounds i32, ptr %B, i64 2
  %bi3 = getelementptr inbounds i32, ptr %B, i64 3
  %v0 = load i32, ptr %A
  %v1 = load i32, ptr %ai1
  %v2 = load i32, ptr %ai2
  %v3 = load i32, ptr %ai3
  %r0 = add nsw i32 %v0, %v1
  %r1 = add nsw i32 %v1, %v2
  %r2 = udiv i32 %v2, %v3
  %r3 = add nsw i32 %v3, %v0
  store i32 %r0, ptr %B
  store i32 %r1, ptr %bi1
  store i32 %r2, ptr %bi2
  store i32 %r3, ptr %bi3
  ret void
}
