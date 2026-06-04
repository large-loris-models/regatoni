; All values are float. spec-inject's i32-only litmus library should
; produce no output (type incompatibility), and the tool should exit
; cleanly with zero written.
define float @f(float %a, float %b) {
  %x = fadd float %a, %b
  %y = fmul float %x, %a
  ret float %y
}
