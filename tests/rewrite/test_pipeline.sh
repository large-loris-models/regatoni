#!/usr/bin/env bash
# End-to-end: full rewrites.json on every fixture; spot-check counts;
# then on a random sample of 100 split_seeds files all outputs must opt -O2.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"
SPEC="$REPO_ROOT/rewrites.json"
SPLIT_SEEDS="$REPO_ROOT/split_seeds"

echo "[test_pipeline]"

# Pass 1 — fixtures.
out="$TMP/out_fixtures"
mkdir -p "$out"
"$BIN" --spec "$SPEC" --input-dir "$F" --output-dir "$out" \
       --summary-output "$TMP/fixtures_summary.json" \
       --quiet >/dev/null 2>&1 || true

written="$(python3 -c "import json; print(json.load(open('$TMP/fixtures_summary.json'))['variants_written'])")"
vfail="$(python3 -c "import json; print(json.load(open('$TMP/fixtures_summary.json'))['variants_verify_failed'])")"
if (( written > 0 )); then ok "fixtures produced $written variants"; else bad "fixtures produced no variants"; fi
if (( vfail == 0 )); then ok "fixtures had 0 verify failures"; else bad "fixtures had $vfail verify failures"; fi

assert_opt_clean "$out" "all fixture variants pass opt -O2"

# Pass 2 — random 100 split_seeds.
if [[ -d "$SPLIT_SEEDS" ]]; then
  sample="$TMP/seed_sample.list"
  find "$SPLIT_SEEDS" -name '*.ll' | shuf -n 100 --random-source=<(yes 42) > "$sample"
  out2="$TMP/out_split"
  mkdir -p "$out2"
  "$BIN" --spec "$SPEC" --file-list "$sample" --output-dir "$out2" \
         --summary-output "$TMP/split_summary.json" \
         --quiet >/dev/null 2>&1 || true
  written2="$(python3 -c "import json; print(json.load(open('$TMP/split_summary.json'))['variants_written'])")"
  vfail2="$(python3 -c "import json; print(json.load(open('$TMP/split_summary.json'))['variants_verify_failed'])")"
  if (( written2 > 0 )); then ok "split_seeds 100→$written2 variants"; else bad "split_seeds produced 0 variants"; fi
  # opt -O2 every file (this is the strongest assertion).
  fail_count=0
  for f in "$out2"/*.ll; do
    "$OPT" -O2 -disable-output "$f" >/dev/null 2>&1 || fail_count=$((fail_count+1))
  done
  if (( fail_count == 0 )); then
    ok "all $written2 split-seed variants survive opt -O2"
  else
    bad "$fail_count / $written2 split-seed variants failed opt -O2"
  fi
else
  ok "split_seeds skipped (directory not present)"
fi

summarize
