#!/usr/bin/env bash
# Exercises add_metadata for range/nonnull/nofpclass.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_metadata]"

# 1. !range on load of integer
spec="$(write_spec md_range '{"rewrites":[{"id":"rng","category":"metadata",
  "match":{"target":"instruction","opcode":["load"],"result_type_class":"integer","metadata_absent":"range"},
  "transform":{"action":"add_metadata","kind":"range","scheme":"nonzero"}}]}')"
out="$TMP/out_md_range"
run_rewrite "$spec" "$F/basic_load.ll" "$out"
assert_match "$out" '!range' "load of int gets !range metadata" >/dev/null

# 2. !nonnull on load of pointer
spec="$(write_spec md_nn '{"rewrites":[{"id":"nn","category":"metadata",
  "match":{"target":"instruction","opcode":["load"],"result_type_class":"pointer","metadata_absent":"nonnull"},
  "transform":{"action":"add_metadata","kind":"nonnull","scheme":"nonnull"}}]}')"
out="$TMP/out_md_nn"
run_rewrite "$spec" "$F/ptr_load.ll" "$out"
assert_match "$out" '!nonnull' "load of ptr gets !nonnull metadata" >/dev/null

# 3. !nofpclass on load of float (no_nan)
spec="$(write_spec md_fpc '{"rewrites":[{"id":"fpc","category":"metadata",
  "match":{"target":"instruction","opcode":["load"],"result_type_class":"float","metadata_absent":"nofpclass"},
  "transform":{"action":"add_metadata","kind":"nofpclass","scheme":"no_nan"}}]}')"
out="$TMP/out_md_fpc"
run_rewrite "$spec" "$F/float_load.ll" "$out"
assert_match "$out" '!nofpclass' "load of float gets !nofpclass metadata" >/dev/null

# All outputs survive opt -O2.
for dir in "$TMP"/out_md_range "$TMP"/out_md_nn "$TMP"/out_md_fpc; do
  assert_opt_clean "$dir" "$(basename "$dir") survives opt -O2"
done

summarize
