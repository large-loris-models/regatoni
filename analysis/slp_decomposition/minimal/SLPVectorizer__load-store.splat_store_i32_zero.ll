define void @splat_store_i32_zero(ptr %dest) {
entry:
  store i32 0, ptr %dest, align 4
  %inc1 = getelementptr inbounds i32, ptr %dest, i64 1
  store i32 0, ptr %inc1, align 2
  %inc2 = getelementptr inbounds i32, ptr %dest, i64 2
  store i32 0, ptr %inc2, align 2
  %inc3 = getelementptr inbounds i32, ptr %dest, i64 3
  store i32 0, ptr %inc3, align 2
  ret void
}
