; S_R3_crossblock_reduction: a 4-way reduction whose loaded operands
; come from TWO different basic blocks joined via a branch+PHI. SLP
; sees the resulting tree as having gather entries with different
; parent BBs, which engages BoUpSLP::isTreeNotExtendable's
; `!allSameBlock(E.Scalars)` branch (lines 17358..17363).
target triple = "x86_64-unknown-linux-gnu"
@p = local_unnamed_addr global [8 x float] zeroinitializer, align 16
@q = local_unnamed_addr global [8 x float] zeroinitializer, align 16

define float @crossblock_reduce(i1 %take_q) {
entry:
  br i1 %take_q, label %side, label %merge

side:
  %q0 = load float, ptr getelementptr inbounds ([8 x float], ptr @q, i64 0, i64 0), align 16
  %q1 = load float, ptr getelementptr inbounds ([8 x float], ptr @q, i64 0, i64 1), align 4
  %q2 = load float, ptr getelementptr inbounds ([8 x float], ptr @q, i64 0, i64 2), align 8
  %q3 = load float, ptr getelementptr inbounds ([8 x float], ptr @q, i64 0, i64 3), align 4
  br label %merge

merge:
  %v0 = phi float [ 0.0, %entry ], [ %q0, %side ]
  %v1 = phi float [ 0.0, %entry ], [ %q1, %side ]
  %v2 = phi float [ 0.0, %entry ], [ %q2, %side ]
  %v3 = phi float [ 0.0, %entry ], [ %q3, %side ]
  %p0 = load float, ptr getelementptr inbounds ([8 x float], ptr @p, i64 0, i64 0), align 16
  %p1 = load float, ptr getelementptr inbounds ([8 x float], ptr @p, i64 0, i64 1), align 4
  %p2 = load float, ptr getelementptr inbounds ([8 x float], ptr @p, i64 0, i64 2), align 8
  %p3 = load float, ptr getelementptr inbounds ([8 x float], ptr @p, i64 0, i64 3), align 4
  %a0 = fadd fast float %p0, %v0
  %a1 = fadd fast float %p1, %v1
  %a2 = fadd fast float %p2, %v2
  %a3 = fadd fast float %p3, %v3
  %r0 = fadd fast float %a0, %a1
  %r1 = fadd fast float %r0, %a2
  %r2 = fadd fast float %r1, %a3
  ret float %r2
}
