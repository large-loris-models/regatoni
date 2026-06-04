#!/usr/bin/env bash
# Inject a litmus that calls an intrinsic. The intrinsic declaration
# must end up in the linked module (the linker merges decls).
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_intrinsic_inject]"

LIT="$TMP/litmus"
HOST="$TMP/host"
OUT="$TMP/out"
stage_dir "$LIT"  "$F/inject_litmus_cttz_i32.ll"
stage_dir "$HOST" "$F/loop_add.ll"

run_inject "$LIT" "$HOST" "$OUT" --max-injections-per-function 5 --seed 11

assert_match "$OUT" 'call i32 @cttz_i32_zp_true' \
  "call to cttz wrapper present" >/dev/null
assert_match "$OUT" 'declare i32 @llvm.cttz.i32' \
  "intrinsic declaration carried over by linker" >/dev/null
assert_match "$OUT" 'define internal i32 @cttz_i32_zp_true' \
  "wrapper marked internal so inliner can fold" >/dev/null
assert_opt_clean "$OUT" "intrinsic-using injection passes opt -O2"

summarize
