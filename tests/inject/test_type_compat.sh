#!/usr/bin/env bash
# Host has only float values; litmus library has only i32 litmus.
# Expectation: zero injections written, tool exits cleanly.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_type_compat]"

LIT="$TMP/litmus"
HOST="$TMP/host"
OUT="$TMP/out"
stage_dir "$LIT"  "$F/inject_litmus_shl_nuw_i32.ll" "$F/inject_litmus_cttz_i32.ll"
stage_dir "$HOST" "$F/inject_float_only.ll"

run_inject "$LIT" "$HOST" "$OUT" --max-injections-per-function 5 --seed 17

assert_no_files "$OUT" "float-only host gives zero injections for i32-only litmus library"

# The tool should still exit 0 (no compatibility is not an error).
# The runner above already swallows the exit code via run_inject; here we
# just re-check it directly.
if "$BIN" --litmus-dir "$LIT" --host-dir "$HOST" --output-dir "$TMP/out2" \
    --quiet >/dev/null 2>"$TMP/stderr2.log"; then
  ok "tool exits 0 on incompatible host"
else
  bad "tool should exit 0 on incompatible host" "got non-zero exit; stderr: $(head -1 "$TMP/stderr2.log")"
fi

summarize
