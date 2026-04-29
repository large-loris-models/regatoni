define i32 @ls4_red(ptr %A, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %lp.ph, label %end
lp.ph:
  %ne = sext i32 %n to i64
  br label %lp
lp:
  %i = phi i64 [ 0, %lp.ph ], [ %inc, %lp ]
  %sum = phi i32 [ 0, %lp.ph ], [ %s4, %lp ]
  %off = shl nsw i64 %i, 2
  %p0 = getelementptr inbounds i32, ptr %A, i64 %off
  %p1 = getelementptr inbounds i32, ptr %p0, i64 1
  %p2 = getelementptr inbounds i32, ptr %p0, i64 2
  %p3 = getelementptr inbounds i32, ptr %p0, i64 3
  %v0 = load i32, ptr %p0, align 4
  %v1 = load i32, ptr %p1, align 4
  %v2 = load i32, ptr %p2, align 4
  %v3 = load i32, ptr %p3, align 4
  %s1 = add i32 %sum, %v0
  %s2 = add i32 %s1, %v1
  %s3 = add i32 %s2, %v2
  %s4 = add i32 %s3, %v3
  %inc = add nsw i64 %i, 1
  %ec = icmp eq i64 %inc, %ne
  br i1 %ec, label %end_pre, label %lp
end_pre:
  br label %end
end:
  %r = phi i32 [ 0, %entry ], [ %s4, %end_pre ]
  ret i32 %r
}
