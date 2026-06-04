#!/usr/bin/env bash
# Inject a simple i32→i32 litmus into a loop host. Verify the call
# is present, the litmus body is in the module, opt -O2 is clean.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_basic_inject]"

LIT="$TMP/litmus"
HOST="$TMP/host"
OUT="$TMP/out"
stage_dir "$LIT"  "$F/inject_litmus_shl_nuw_i32.ll"
stage_dir "$HOST" "$F/loop_add.ll"

run_inject "$LIT" "$HOST" "$OUT" --max-injections-per-function 5 --seed 7

assert_file_exists "$OUT" 'loop_add__inject_inject_litmus_shl_nuw_i32.ll' \
  "produced loop_add × shl_nuw_i32 output" >/dev/null
assert_match "$OUT" 'call i32 @shl_nuw_i32' "call to litmus function present" >/dev/null
assert_match "$OUT" 'define internal i32 @shl_nuw_i32' \
  "litmus function present with internal linkage" >/dev/null
assert_opt_clean "$OUT" "injected module passes opt -O2"

summarize
