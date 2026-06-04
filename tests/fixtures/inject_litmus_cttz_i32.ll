; Single-arg i32 litmus that calls an intrinsic — exercises the
; intrinsic-decl merging path of llvm::Linker::linkModules.
declare i32 @llvm.cttz.i32(i32, i1 immarg)

define i32 @cttz_i32_zp_true(i32 %op1) {
  %r = call i32 @llvm.cttz.i32(i32 %op1, i1 true)
  ret i32 %r
}
