; S_R3_reuse_reduction: 8-wide fadd reduction over only TWO unique
; scalars, each used 4 times. This activates the
; HorizontalReduction::emitScaleForReusedOps + emitReusedOps path
; (Scale > 1 in emitReduction).
target triple = "x86_64-unknown-linux-gnu"

define float @reuse_reduce(float %a, float %b) {
entry:
  %s0 = fadd fast float %a, %a
  %s1 = fadd fast float %s0, %a
  %s2 = fadd fast float %s1, %a
  %s3 = fadd fast float %s2, %b
  %s4 = fadd fast float %s3, %b
  %s5 = fadd fast float %s4, %b
  %s6 = fadd fast float %s5, %b
  ret float %s6
}
