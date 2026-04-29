declare i32 @llvm.uadd.sat.i32(i32, i32)
define i32 @ls4_intr_loop(ptr %A, i32 %n) {
entry:
  %cmp = icmp sgt i32 %n, 0
  br i1 %cmp, label %lp, label %end
lp:
  %i = phi i32 [ 0, %entry ], [ %inc, %lp ]
  %sum = phi i32 [ 0, %entry ], [ %s4, %lp ]
  %off = mul nsw i32 %i, 4
  %off64 = sext i32 %off to i64
  %p0 = getelementptr inbounds i32, ptr %A, i64 %off64
  %p1 = getelementptr inbounds i32, ptr %p0, i64 1
  %p2 = getelementptr inbounds i32, ptr %p0, i64 2
  %p3 = getelementptr inbounds i32, ptr %p0, i64 3
  %v0 = load i32, ptr %p0, align 4
  %v1 = load i32, ptr %p1, align 4
  %v2 = load i32, ptr %p2, align 4
  %v3 = load i32, ptr %p3, align 4
  %a0 = call i32 @llvm.uadd.sat.i32(i32 %v0, i32 1)
  %a1 = call i32 @llvm.uadd.sat.i32(i32 %v1, i32 2)
  %a2 = call i32 @llvm.uadd.sat.i32(i32 %v2, i32 3)
  %a3 = call i32 @llvm.uadd.sat.i32(i32 %v3, i32 4)
  %s1 = add i32 %sum, %a0
  %s2 = add i32 %s1, %a1
  %s3 = add i32 %s2, %a2
  %s4 = add i32 %s3, %a3
  %inc = add nsw i32 %i, 1
  %ec = icmp eq i32 %inc, %n
  br i1 %ec, label %end, label %lp
end:
  %r = phi i32 [ 0, %entry ], [ %s4, %lp ]
  ret i32 %r
}
