; Tiny 2-arg i32 litmus for spec-inject tests.
define i32 @shl_nuw_i32(i32 %a, i32 %b) {
  %r = shl nuw i32 %a, %b
  ret i32 %r
}
