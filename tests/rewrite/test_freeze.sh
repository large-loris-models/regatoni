#!/usr/bin/env bash
# Exercises insert_freeze.
source "$(dirname "$0")/lib.sh"
F="$REPO_ROOT/tests/fixtures"

echo "[test_freeze]"

# Insert freeze before sdiv divisor.
spec="$(write_spec fz_sdiv '{"rewrites":[{"id":"fz","category":"freeze",
  "match":{"target":"instruction","opcode":["sdiv"],"operand_index":1},
  "transform":{"action":"insert_freeze","on_operand":1}}]}')"
out="$TMP/out_fz_sdiv"
run_rewrite "$spec" "$F/basic_sdiv.ll" "$out"

# The output should contain a freeze instruction followed by an sdiv
# that uses that frozen value.
file="$(assert_match "$out" 'freeze i32' "freeze instruction present")"
if [[ -n "$file" ]]; then
  # capture the freeze SSA name (e.g. %1) then verify sdiv uses it
  fname="$(grep -oE '%[A-Za-z_0-9.]+ = freeze i32' "$file" | head -1 | awk '{print $1}')"
  if [[ -z "$fname" ]]; then
    fname="%1"   # unnamed freeze numbered by the printer
  fi
  if grep -qE "sdiv i32 %[A-Za-z_0-9.]+, $fname([^A-Za-z_0-9.]|$)" "$file"; then
    ok "sdiv uses the frozen operand"
  else
    bad "sdiv uses the frozen operand" "$file does not feed sdiv from the freeze"
  fi
fi

assert_opt_clean "$out" "freeze outputs survive opt -O2"
summarize
