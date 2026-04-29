define i16 @test() {
;
entry:
  %pPrev.058.i = getelementptr %S, ptr null, i64 -1
  br label %while.body.i

while.body.i:
  %0 = phi i16 [ 0, %while.body.i ], [ 0, %entry ]
  %pPrev.062.i = phi ptr [ %pPrev.0.i, %while.body.i ], [ %pPrev.058.i, %entry ]
  %pEdge.061.i = phi ptr [ %incdec.ptr.i, %while.body.i ], [ null, %entry ]
  %incdec.ptr.i = getelementptr %S, ptr %pEdge.061.i, i64 -1
  %pPrev.0.i = getelementptr %S, ptr %pPrev.062.i, i64 -1
  %1 = load i16, ptr %incdec.ptr.i, align 2
  %2 = load i16, ptr %pPrev.0.i, align 2
  %cmp.i178 = icmp ult i16 %1, %2
  br label %while.body.i
}
