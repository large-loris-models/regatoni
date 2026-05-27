#!/usr/bin/env bash
# Exercises the add_flag / remove_flag actions.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_flags]"

# 1. add_nsw on basic_add.ll → output has "add nsw i32"
spec="$(write_spec add_nsw '{"rewrites":[{"id":"add_nsw","category":"flag",
  "match":{"target":"instruction","opcode":["add"],"flag_absent":"nsw"},
  "transform":{"action":"add_flag","flag":"nsw"}}]}')"
out="$TMP/out_add_nsw"
run_rewrite "$spec" "$F/basic_add.ll" "$out"
assert_match "$out" 'add nsw i32' "add_nsw produces 'add nsw i32'" >/dev/null

# 2. remove_nsw on flagged_add.ll → output has "add i32" without nsw
spec="$(write_spec strip_nsw '{"rewrites":[{"id":"strip","category":"flag",
  "match":{"target":"instruction","opcode":["add"],"flag_present":"nsw"},
  "transform":{"action":"remove_flag","flag":"nsw"}}]}')"
out="$TMP/out_strip_nsw"
run_rewrite "$spec" "$F/flagged_add.ll" "$out"
assert_match "$out" '^[[:space:]]*%r = add i32' "remove_nsw drops nsw" >/dev/null
assert_no_match "$out" 'add nsw i32' "remove_nsw leaves no nsw"

# 3. add_exact on basic_sdiv.ll → "sdiv exact i32"
spec="$(write_spec add_exact '{"rewrites":[{"id":"sdiv_exact","category":"flag",
  "match":{"target":"instruction","opcode":["sdiv"],"flag_absent":"exact"},
  "transform":{"action":"add_flag","flag":"exact"}}]}')"
out="$TMP/out_sdiv_exact"
run_rewrite "$spec" "$F/basic_sdiv.ll" "$out"
assert_match "$out" 'sdiv exact i32' "add_exact produces 'sdiv exact i32'" >/dev/null

# 4. add_inbounds on basic_gep.ll → "getelementptr inbounds"
spec="$(write_spec add_inb '{"rewrites":[{"id":"gep_inb","category":"flag",
  "match":{"target":"instruction","opcode":["getelementptr"],"flag_absent":"inbounds"},
  "transform":{"action":"add_flag","flag":"inbounds"}}]}')"
out="$TMP/out_gep_inb"
run_rewrite "$spec" "$F/basic_gep.ll" "$out"
assert_match "$out" 'getelementptr inbounds' "add_inbounds on gep" >/dev/null

# Every produced file must still opt -O2.
assert_opt_clean "$TMP/out_add_nsw"    "add_nsw outputs survive opt -O2"
assert_opt_clean "$TMP/out_strip_nsw"  "strip_nsw outputs survive opt -O2"
assert_opt_clean "$TMP/out_sdiv_exact" "sdiv_exact outputs survive opt -O2"
assert_opt_clean "$TMP/out_gep_inb"    "gep_inbounds outputs survive opt -O2"

summarize
