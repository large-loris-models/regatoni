; S_R3_loop_reduction_phi: counted loop with a 2-incoming-value
; reduction PHI of the canonical `phi i32 [0, %entry], [%sum.next,
; %loop]` form. Hits SLPVectorizerPass::vectorizeChainsInBlock
; line 29753 `if (P->getNumIncomingValues() == 2)` and routes into
; the `getReductionInstr` helper that picks the reduction-op operand.
target triple = "x86_64-unknown-linux-gnu"
@arr = local_unnamed_addr global [128 x i32] zeroinitializer, align 16

define i32 @loop_reduction(i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %sum = phi i32 [ 0, %entry ], [ %sum.next, %loop ]

  %p = getelementptr inbounds [128 x i32], ptr @arr, i64 0, i64 %i
  %x = load i32, ptr %p, align 4
  %sum.next = add nsw i32 %sum, %x

  %i.next = add nuw nsw i64 %i, 1
  %nz = zext i32 %n to i64
  %cmp = icmp ult i64 %i.next, %nz
  br i1 %cmp, label %loop, label %exit

exit:
  ret i32 %sum.next
}
