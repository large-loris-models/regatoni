; Diamond CFG: %a is defined only in branchA, %b only in branchB,
; %m is the post-merge value. spec-inject must never pass %a or %b
; into a call that is reachable from the other branch, and must
; never insert a call whose downstream RAUW would cross-dominate
; into a parallel branch. The LLVM verifier enforces both invariants.
define i32 @diamond(i32 %x, i1 %cond) {
entry:
  br i1 %cond, label %branchA, label %branchB

branchA:
  %a = add i32 %x, 1
  br label %merge

branchB:
  %b = sub i32 %x, 1
  br label %merge

merge:
  %m = phi i32 [ %a, %branchA ], [ %b, %branchB ]
  %r = mul i32 %m, %x
  ret i32 %r
}
