define i32 @f(i32 %n) {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%i.next, %loop]
  %acc = phi i32 [0, %entry], [%sum, %loop]
  %sum = add i32 %acc, %i
  %i.next = add i32 %i, 1
  %cond = icmp slt i32 %i.next, %n
  br i1 %cond, label %loop, label %exit
exit:
  ret i32 %sum
}
