#!/usr/bin/env bash
# Diamond host: %a defined in branchA, %b in branchB, %m in merge.
# spec-inject must never pick non-dominating values for call args
# or downstream-RAUW use slots. The LLVM verifier is the authority
# here: any dominance violation triggers verifyModule failure inside
# the tool, which then skips the candidate (recorded as verify_fail).
# We assert the produced outputs all pass an external opt -verify and
# opt -O2 round.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_dominance]"

LIT="$TMP/litmus"
HOST="$TMP/host"
OUT="$TMP/out"
stage_dir "$LIT"  "$F/inject_litmus_shl_nuw_i32.ll" "$F/inject_litmus_cttz_i32.ll"
stage_dir "$HOST" "$F/inject_diamond.ll"

run_inject "$LIT" "$HOST" "$OUT" --max-injections-per-function 5 --seed 23

# Diamond has i32-typed values, so at least one injection should land.
hit="$(find "$OUT" -maxdepth 1 -name 'inject_diamond__inject_*.ll' | head -1 || true)"
if [[ -z "$hit" ]]; then
  bad "diamond produced at least one injection" "no inject_diamond__* output found"
else
  ok "diamond produced at least one injection"
fi

# Every produced output must pass opt -verify; this catches any
# dominance violation that escaped the in-tool verifier (would be a bug).
shopt -s nullglob
for f in "$OUT"/*.ll; do
  if ! "$OPT" -passes=verify -disable-output "$f" >/dev/null 2>"$TMP/v.err"; then
    bad "$(basename "$f") passes opt -verify" "$(head -1 "$TMP/v.err")"
  else
    ok "$(basename "$f") passes opt -verify"
  fi
done
shopt -u nullglob

assert_opt_clean "$OUT" "diamond injections pass opt -O2"

summarize
